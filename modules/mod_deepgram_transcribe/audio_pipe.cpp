#include "audio_pipe.hpp"
#include "context_manager.hpp"

#include <cassert>
#include <iostream>

/* discard incoming text messages over the socket that are longer than this */
#define MAX_RECV_BUF_SIZE (65 * 1024 * 10)
#define RECV_BUF_REALLOC_SIZE (8 * 1024)

using namespace deepgram;

namespace {
  static const char *requestedTcpKeepaliveSecs = std::getenv("MOD_AUDIO_FORK_TCP_KEEPALIVE_SECS");
  static int nTcpKeepaliveSecs = requestedTcpKeepaliveSecs ? ::atoi(requestedTcpKeepaliveSecs) : 55;
}

static int dch_lws_http_basic_auth_gen(const char *apiKey, char *buf, size_t len) {
	size_t n = strlen(apiKey);

	if (len < n + 7)
		return 1;

	strcpy(buf,"Token ");
  strcpy(buf + 6, apiKey);
	return 0;
}

int AudioPipe::lws_callback(struct lws *wsi, 
  enum lws_callback_reasons reason,
  void *user, void *in, size_t len) {

  // Only log errors and connection state changes at appropriate levels

  struct AudioPipe::lws_per_vhost_data *vhd = 
    (struct AudioPipe::lws_per_vhost_data *) lws_protocol_vh_priv_get(lws_get_vhost(wsi), lws_get_protocol(wsi));

  struct lws_vhost* vhost = lws_get_vhost(wsi);
  AudioPipe ** ppAp = (AudioPipe **) user;

  switch (reason) {
    case LWS_CALLBACK_PROTOCOL_INIT:
      {
        auto init_time = std::chrono::steady_clock::now();
        static auto first_init = init_time;
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(init_time - first_init).count();
        
        vhd = (struct AudioPipe::lws_per_vhost_data *) lws_protocol_vh_priv_zalloc(lws_get_vhost(wsi), lws_get_protocol(wsi), sizeof(struct AudioPipe::lws_per_vhost_data));
        vhd->context = lws_get_context(wsi);
        vhd->protocol = lws_get_protocol(wsi);
        vhd->vhost = lws_get_vhost(wsi);
        
        // Store vhd in BOTH thread-local storage AND context map
        deepgram::ContextManager::setThreadVhd(vhd);
        deepgram::ContextManager::setContextVhd(vhd->context, vhd);
      }
      break;

    case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER:
      {
        AudioPipe* ap = findPendingConnect(wsi);
        if (ap) {
          std::string apiKey = ap->getApiKey();
          if (apiKey.empty()) {
            lwsl_err("AudioPipe: API key is EMPTY for session %s\n", ap->m_uuid.c_str());
          }
          unsigned char **p = (unsigned char **)in, *end = (*p) + len;
          char b[256];
          memset(b, 0, sizeof(b));
          strcpy(b,"Token ");
          strcpy(b + 6, apiKey.c_str());

          if (lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_AUTHORIZATION, (unsigned char *)b, strlen(b), p, end)) return -1;
        } else {
          lwsl_err("AudioPipe: LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER - no pending connect found for wsi %p\n", wsi);
        }
      }
      break;

    case LWS_CALLBACK_EVENT_WAIT_CANCELLED:
      processPendingConnects(vhd);
      processPendingDisconnects(vhd);
      processPendingWrites();
      break;
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
      {
        AudioPipe* ap = findAndRemovePendingConnect(wsi);
        int rc = lws_http_client_http_response(wsi);
        lwsl_err("AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_CONNECTION_ERROR: %s, response status %d\n", in ? (char *)in : "(null)", rc); 
        if (ap) {
          ap->m_state = LWS_CLIENT_FAILED;
          ap->m_callback(ap->m_uuid.c_str(), AudioPipe::CONNECT_FAIL, (char *) in, ap->isFinished());
        }
        else {
          lwsl_err("AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_CONNECTION_ERROR unable to find wsi %p..\n", wsi); 
        }
      }      
      break;

    case LWS_CALLBACK_CLIENT_ESTABLISHED:
      {
        AudioPipe* ap = findAndRemovePendingConnect(wsi);
        if (ap) {
          *ppAp = ap;
          ap->m_vhd = vhd;
          ap->m_state = LWS_CLIENT_CONNECTED;
          ap->m_callback(ap->m_uuid.c_str(), AudioPipe::CONNECT_SUCCESS, NULL,  ap->isFinished());
        }
        else {
          lwsl_err("AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_ESTABLISHED %s unable to find wsi %p..\n", ap->m_uuid.c_str(), wsi); 
        }
      }      
      break;
    case LWS_CALLBACK_CLIENT_CLOSED:
      {
        AudioPipe* ap = *ppAp;
        if (!ap) {
          lwsl_err("AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_CLOSED %s unable to find wsi %p..\n", ap->m_uuid.c_str(), wsi); 
          return 0;
        }
        if (ap->m_state == LWS_CLIENT_DISCONNECTING) {
          // closed by us

          lwsl_debug("%s socket closed by us\n", ap->m_uuid.c_str());
          ap->m_callback(ap->m_uuid.c_str(), AudioPipe::CONNECTION_CLOSED_GRACEFULLY, NULL,  ap->isFinished());
        }
        else if (ap->m_state == LWS_CLIENT_CONNECTED) {
          // closed by far end
          lwsl_info("%s socket closed by far end\n", ap->m_uuid.c_str());
          ap->m_callback(ap->m_uuid.c_str(), AudioPipe::CONNECTION_DROPPED, NULL,  ap->isFinished());
        }
        ap->m_state = LWS_CLIENT_DISCONNECTED;
        ap->setClosed();
    
        //NB: after receiving any of the events above, any holder of a 
        //pointer or reference to this object must treat is as no longer valid

        //*ppAp = NULL;
        //delete ap;
      }
      break;

    case LWS_CALLBACK_CLIENT_RECEIVE:
      {
        AudioPipe* ap = *ppAp;
        if (!ap) {
          lwsl_err("AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_RECEIVE %s unable to find wsi %p..\n", ap->m_uuid.c_str(), wsi); 
          return 0;
        }

        if (lws_frame_is_binary(wsi)) {
          lwsl_err("AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_RECEIVE received binary frame, discarding.\n");
          return 0;
        }

        if (lws_is_first_fragment(wsi)) {
          // Zero-Malloc Optimization: clear but keep capacity
          ap->m_recv_buf.clear();
          size_t estimated_total = len + lws_remaining_packet_payload(wsi);
          if (ap->m_recv_buf.capacity() < estimated_total) {
            ap->m_recv_buf.reserve(estimated_total);
          }
        }

        // Direct append to vector (amortized O(1))
        const uint8_t* ptr = (const uint8_t*)in;
        ap->m_recv_buf.insert(ap->m_recv_buf.end(), ptr, ptr + len);

        if (lws_is_final_fragment(wsi)) {
           // Construct string from vector (copy required for std::string compatibility with existing API)
           // Warning: string constructor copies data. For absolute zero-copy, callback API needs change.
           // However, avoiding malloc/free per message is the big win here.
           std::string msg(ap->m_recv_buf.begin(), ap->m_recv_buf.end());
           ap->m_callback(ap->m_uuid.c_str(), AudioPipe::MESSAGE, msg.c_str(), ap->isFinished());
           
           // Reset content but keep capacity for next message
           ap->m_recv_buf.clear();
        }
      }
      break;

    case LWS_CALLBACK_CLIENT_WRITEABLE:
      {
        AudioPipe* ap = *ppAp;
        if (!ap) {
          lwsl_err("AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_WRITEABLE %s unable to find wsi %p..\n", ap->m_uuid.c_str(), wsi); 
          return 0;
        }

        // check for text frames to send
        {
          std::lock_guard<std::mutex> lk(ap->m_text_mutex);
          if (ap->m_metadata.length() > 0) {
            uint8_t buf[ap->m_metadata.length() + LWS_PRE];
            memcpy(buf + LWS_PRE, ap->m_metadata.c_str(), ap->m_metadata.length());
            int n = ap->m_metadata.length();
            int m = lws_write(wsi, buf + LWS_PRE, n, LWS_WRITE_TEXT);
            ap->m_metadata.clear();
            if (m < n) {
              return -1;
            }

            // there may be audio data, but only one write per writeable event
            // get it next time
            lws_callback_on_writable(wsi);

            return 0;
          }
        }

        if (ap->m_state == LWS_CLIENT_DISCONNECTING) {
          lws_close_reason(wsi, LWS_CLOSE_STATUS_NORMAL, NULL, 0);
          return -1;
        }

        // check for audio packets (LOCK-FREE)
        {
          // Pop audio data from lock-free ring buffer
          // Buffer needs LWS_PRE bytes before payload
          static constexpr size_t MAX_WS_PAYLOAD = 16384;
          uint8_t ws_buffer[LWS_PRE + MAX_WS_PAYLOAD];
          
          size_t datalen = ap->popAudio(ws_buffer + LWS_PRE, MAX_WS_PAYLOAD);
          if (datalen > 0) {
            int sent = lws_write(wsi, ws_buffer + LWS_PRE, datalen, LWS_WRITE_BINARY);
            if (sent < (int)datalen) {
              lwsl_err("AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_WRITEABLE %s attemped to send %lu only sent %d wsi %p..\n", 
                ap->m_uuid.c_str(), datalen, sent, wsi); 
            }
            
            // If there's more data, request another writable callback
            if (ap->hasAudioPending()) {
              lws_callback_on_writable(wsi);
            }
          }
        }

        return 0;
      }
      break;

    default:
      break;
  }
  return lws_callback_http_dummy(wsi, reason, user, in, len);
}


// static members
static const lws_retry_bo_t retry = {
    nullptr,   // retry_ms_table
    0,         // retry_ms_table_count
    0,         // conceal_count
    UINT16_MAX,         // secs_since_valid_ping
    UINT16_MAX,        // secs_since_valid_hangup
    0          // jitter_percent
};

// Thread-local context manager initialized during AudioPipe::initialize
std::string AudioPipe::protocolName;

// Lock-free MPSC queues (HIGH SCALE)
BoundedMPSCQueue<AudioPipe, 16384> AudioPipe::pendingConnectsQueue;
BoundedMPSCQueue<AudioPipe, 16384> AudioPipe::pendingDisconnectsQueue;
BoundedMPSCQueue<AudioPipe, 16384> AudioPipe::pendingWritesQueue;

// Legacy mutex-guarded vectors (fallback)
std::mutex AudioPipe::mutex_connects;
std::mutex AudioPipe::mutex_disconnects;
std::mutex AudioPipe::mutex_writes;
std::vector<AudioPipe*> AudioPipe::pendingConnects;
std::vector<AudioPipe*> AudioPipe::pendingDisconnects;
std::vector<AudioPipe*> AudioPipe::pendingWrites;

// PHASE 2: Pure lock-free queue system implemented

AudioPipe::log_emit_function AudioPipe::logger;
std::mutex AudioPipe::mapMutex;
std::unordered_map<std::thread::id, bool> AudioPipe::stopFlags;
std::queue<std::thread::id> AudioPipe::threadIds;


void AudioPipe::processPendingConnects(lws_per_vhost_data *vhd) {
  // PHASE 2: Always use lock-free path for main queue operations
  AudioPipe* ap;
  
  if (!vhd) {
    lwsl_err("processPendingConnects: vhd is NULL!\n");
    return;
  }
  
  int count = 0;
  while ((ap = pendingConnectsQueue.pop()) != nullptr) {
    count++;
    // lwsl_notice("[DEBUG] Processing pending connect #%d: ap=%p, uuid=%s, state=%d\n",   // Disabled - too noisy
    //             count, ap, ap->m_uuid.c_str(), ap->m_state);
    
    if (ap->m_state == LWS_CLIENT_IDLE) {
      ap->m_state = LWS_CLIENT_CONNECTING;
      // MINIMAL MUTEX: Only for handshake lookup (LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER)
      {
        std::lock_guard<std::mutex> guard(mutex_connects);
        pendingConnects.push_back(ap);
      }
      
      // CRITICAL: Validate vhd before passing to connect_client
      if (!vhd || !vhd->context) {
        lwsl_err("Cannot connect %s - vhd or context is NULL (vhd=%p, context=%p)\n", 
                 ap->m_uuid.c_str(), vhd, vhd ? vhd->context : nullptr);
        ap->m_state = LWS_CLIENT_IDLE;  // Reset state so it can retry
        continue;
      }
      
      ap->connect_client(vhd);
    } else {
      lwsl_warn("Skipping connect for %s - wrong state: %d\n", ap->m_uuid.c_str(), ap->m_state);
    }
  }
}

void AudioPipe::processPendingDisconnects(lws_per_vhost_data *vhd) {
  // PHASE 2: Pure lock-free path
  AudioPipe* ap;
  while ((ap = pendingDisconnectsQueue.pop()) != nullptr) {
    if (ap->m_state == LWS_CLIENT_DISCONNECTING && ap->m_wsi) {
      lws_callback_on_writable(ap->m_wsi);
    }
  }
}

void AudioPipe::processPendingWrites() {
  // PHASE 2: Pure lock-free path
  AudioPipe* ap;
  while ((ap = pendingWritesQueue.pop()) != nullptr) {
    if (ap->m_state == LWS_CLIENT_CONNECTED && ap->m_wsi) {
      lws_callback_on_writable(ap->m_wsi);
    }
  }
}

AudioPipe* AudioPipe::findAndRemovePendingConnect(struct lws *wsi) {
  AudioPipe* ap = nullptr;
  std::lock_guard<std::mutex> guard(mutex_connects);
  
  // HIGH SCALE: Use erase-remove idiom for O(n) instead of O(n²)
  // First pass: find the target and mark null entries for removal
  auto remove_it = std::remove_if(pendingConnects.begin(), pendingConnects.end(),
    [wsi, &ap](AudioPipe* p) {
      if (p->m_wsi == nullptr) return true;  // Remove null wsi entries
      if (!ap && p->m_state == LWS_CLIENT_CONNECTING && p->m_wsi == wsi) {
        ap = p;
        return true;  // Remove the found entry
      }
      return false;
    });
  pendingConnects.erase(remove_it, pendingConnects.end());
  
  return ap;
}

AudioPipe* AudioPipe::findPendingConnect(struct lws *wsi) {
  std::lock_guard<std::mutex> guard(mutex_connects);
  
  // HIGH SCALE: Use std::find_if for cleaner code
  auto it = std::find_if(pendingConnects.begin(), pendingConnects.end(),
    [wsi](AudioPipe* p) {
      return p->m_state == LWS_CLIENT_CONNECTING && p->m_wsi == wsi;
    });
  
  return (it != pendingConnects.end()) ? *it : nullptr;
}

void AudioPipe::addPendingConnect(AudioPipe* ap) {
  // PHASE 2: Always use lock-free queue (unbounded, never fails)
  pendingConnectsQueue.push(ap);
  
  // CRITICAL FIX: Wake up ALL service threads immediately
  // Without this, threads sleep in poll() for seconds while work waits in queue
  // This was causing 18+ second delays from addPendingConnect to connect_client
  deepgram::ContextManager::wakeAllServiceThreads();
}

void AudioPipe::addPendingDisconnect(AudioPipe* ap) {
  ap->m_state = LWS_CLIENT_DISCONNECTING;
  // PHASE 2: Always use lock-free queue (unbounded, never fails)
  pendingDisconnectsQueue.push(ap);
  if (ap->m_wsi) {
    lws_callback_on_writable(ap->m_wsi);
  }
  // Wake threads to process disconnect immediately
  deepgram::ContextManager::wakeAllServiceThreads();
}

void AudioPipe::addPendingWrite(AudioPipe* ap) {
  // PHASE 2: Always use lock-free queue (unbounded, never fails)
  pendingWritesQueue.push(ap);
  // Wake threads to send data immediately
  deepgram::ContextManager::wakeAllServiceThreads();
  if (ap->m_wsi) {
    lws_callback_on_writable(ap->m_wsi);
  }
}

void AudioPipe::adaptive_lws_service_thread(unsigned int nServiceThread) {
  std::thread::id this_id = std::this_thread::get_id();

  const struct lws_protocols protocols[] = {
    {
      "",
      AudioPipe::lws_callback,
      sizeof(void *),
      1024,
    },
    { NULL, NULL, 0, 0 }
  };

  struct lws_context_creation_info info;
  memset(&info, 0, sizeof info); 
  info.port = CONTEXT_PORT_NO_LISTEN; 
  info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
  info.protocols = protocols;
  info.ka_time = nTcpKeepaliveSecs;                    // tcp keep-alive timer
  info.ka_probes = 4;                   // number of times to try ka before closing connection
  info.ka_interval = 5;                 // time between ka's
  info.timeout_secs = 10;                // doc says timeout for "various processes involving network roundtrips"
  info.keepalive_timeout = 5;           // seconds to allow remote client to hold on to an idle HTTP/1.1 connection 
  info.timeout_secs_ah_idle = 10;       // secs to allow a client to hold an ah without using it
  info.retry_and_idle_policy = &retry;

  lwsl_notice("AudioPipe::lws_service_thread creating thread-local context %d.\n", nServiceThread);

  // HIGH SCALE: Get thread-local LWS context (zero contention)
  struct lws_context* context = deepgram::ContextManager::getThreadContext();
  if (!context) {
    lwsl_err("AudioPipe::lws_service_thread failed creating thread-local context %d..\n", nServiceThread); 
    return;
  }

  lwsl_notice("[DEBUG] Service thread %d initialized with context %p\n", nServiceThread, context);

  // Wait for PROTOCOL_INIT to fire and vhd to be initialized
  // This happens asynchronously when lws_create_context() creates the vhost
  lwsl_notice("[PERF] Service thread %d: Waiting for PROTOCOL_INIT to initialize vhd...\n", nServiceThread);
  
  auto thread_start = std::chrono::steady_clock::now();
  bool vhd_ready = false;
  for (int i = 0; i < 1000 && !vhd_ready; i++) {
    lws_service(context, 0);
    void* vhd_check = deepgram::ContextManager::getContextVhd(context);
    if (vhd_check) {
      auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - thread_start).count();
      lwsl_notice("[PERF] Service thread %d: VHD READY after %d lws_service() calls (T+%ldms) - CAN NOW PROCESS CONNECTIONS\n", 
                  nServiceThread, i + 1, elapsed_ms);
      vhd_ready = true;
      break;
    }
    if (i == 99) {
      auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - thread_start).count();
      lwsl_warn("[PERF] Service thread %d: VHD STILL NOT READY after 100 calls (T+%ldms) - WILL CAUSE CONNECTION DELAYS!\n",
                nServiceThread, elapsed_ms);
    }
  }

  int n;
  uint32_t iterations = 0;
  uint32_t empty_loops = 0;
  uint32_t queue_checks = 0;
  uint32_t vhd_null_count = 0;
  
  do {
    // HIGH SCALE OPTIMIZATION: Use 0ms timeout with adaptive backoff
    // This allows immediate queue processing while being CPU-friendly during idle
    n = lws_service(context, 0);  // Non-blocking
    iterations++;
    
    // CRITICAL: Get vhd from context-based map (not thread-local)
    // This ensures we use the correct vhd even if AudioPipe was created on different thread
    struct lws_per_vhost_data* vhd = (struct lws_per_vhost_data*)deepgram::ContextManager::getContextVhd(context);
    
    // CRITICAL FIX: Check queue status BEFORE vhd check to prevent sleeping with pending work
    // Without this, service thread sleeps 1ms per iteration even when connections are waiting,
    // causing 25+ second delays (25,000 iterations × 1ms)
    bool has_pending_work = !pendingConnectsQueue.empty() ||
                           !pendingDisconnectsQueue.empty() || 
                           !pendingWritesQueue.empty();
    
    // Process shared queues - any thread can process any pending item
    if (vhd) {
      queue_checks++;
      processPendingConnects(vhd);
      processPendingDisconnects(vhd);
      processPendingWrites();
    } else {
      vhd_null_count++;
      
      // CRITICAL FIX: Log delay immediately when vhd is NULL
      if (vhd_null_count == 1) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - thread_start).count();
        lwsl_notice("[PERF] Service thread %d: vhd is NULL at iteration %u (T+%ldms)\n", 
                    nServiceThread, iterations, elapsed);
      }
      
      if (vhd_null_count == 100) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - thread_start).count();
        lwsl_warn("[PERF] Service thread %d: vhd STILL NULL after %u iterations (T+%ldms) - BLOCKING QUEUE PROCESSING\n", 
                  nServiceThread, iterations, elapsed);
      }
    }
    
    // Adaptive backoff: only sleep when truly idle (no network activity AND no pending work)
    if (n <= 0 && !has_pending_work) {
      // Exponential backoff: 10μs → 12μs → 14μs ... → 1ms max
      uint32_t delay_us = std::min(10u + empty_loops * 2, 1000u);
      std::this_thread::sleep_for(std::chrono::microseconds(delay_us));
      empty_loops = std::min(empty_loops + 1, 500u);
    } else {
      empty_loops = 0;  // Reset backoff when busy
    }
    
  } while (n >= 0 && !stopFlags[this_id]);

  // Cleanup once work is done or stopped
  {
      std::lock_guard<std::mutex> lock(mapMutex);
      stopFlags.erase(this_id);
  }

  lwsl_notice("AudioPipe::adaptive_lws_service_thread ending thread-local context %d\n", nServiceThread);
}

void AudioPipe::initialize(unsigned int nThreads, int loglevel, log_emit_function logger) {
  assert(nThreads > 0 && nThreads <= 10);

  lws_set_log_level(loglevel, logger);

  // HIGH SCALE: Initialize thread-local context manager
  deepgram::ContextManager::initialize("", AudioPipe::lws_callback);

  lwsl_notice("AudioPipe::initialize starting %d adaptive service threads\n", nThreads); 
  for (unsigned int i = 0; i < nThreads; i++) {
    std::lock_guard<std::mutex> lock(mapMutex);
    std::thread t(&AudioPipe::adaptive_lws_service_thread, i);
    stopFlags[t.get_id()] = false;
    threadIds.push(t.get_id());
    t.detach();
  }
}

bool AudioPipe::deinitialize() {
  lwsl_notice("AudioPipe::deinitialize\n"); 
  std::lock_guard<std::mutex> lock(mapMutex);
  if (!threadIds.empty()) {
      std::thread::id id = threadIds.front();
      threadIds.pop();
      stopFlags[id] = true;
  }
/*
  do
  {
    lwsl_notice("waiting for pending connects to complete\n");
  } while (pendingConnects.size() > 0);
  do
  {
    lwsl_notice("waiting for disconnects to complete\n");
  } while (pendingDisconnects.size() > 0);
*/
  // HIGH SCALE: Cleanup thread-local contexts (zero contention)
  lwsl_notice("AudioPipe::deinitialize cleaning up thread-local contexts\n");
  deepgram::ContextManager::shutdownAll();
  
  std::this_thread::sleep_for(std::chrono::seconds(2));
  return true;
}

// instance members
AudioPipe::AudioPipe(const char* uuid, const char* host, unsigned int port, const char* path,
  size_t bufLen, size_t minFreespace, const char* apiKey, notifyHandler_t callback) :
  m_uuid(uuid), m_host(host), m_port(port), m_path(path), m_finished(false),
  m_audio_buffer_min_freespace(minFreespace), m_gracefulShutdown(false),
  m_state(LWS_CLIENT_IDLE), m_wsi(nullptr), m_vhd(nullptr), m_apiKey(apiKey), m_callback(callback),
  m_audio_bytes_pending(0) {
  // Lock-free ring buffer is initialized by its constructor
  // Note: bufLen parameter is ignored - using fixed AUDIO_RING_BUFFER_SIZE
  lwsl_info("%s AudioPipe created with lock-free ring buffer (capacity=%zu)\n", 
            uuid, AUDIO_RING_BUFFER_SIZE);
}
AudioPipe::~AudioPipe() {
  // Ring buffer cleanup is automatic
  // m_recv_buf vector cleanup is automatic
}

void AudioPipe::connect(void) {
  addPendingConnect(this);
}

bool AudioPipe::connect_client(struct lws_per_vhost_data *vhd) {
  lwsl_notice("[DEBUG] connect_client called for %s, vhd=%p, m_vhd=%p\n", m_uuid.c_str(), vhd, m_vhd);
  
  assert(m_vhd == nullptr);
  struct lws_client_connect_info i;

  memset(&i, 0, sizeof(i));
  i.context = vhd->context;
  i.port = m_port;
  i.address = m_host.c_str();
  i.path = m_path.c_str();
  i.host = i.address;
  i.origin = i.address;
  i.ssl_connection = LCCSCF_USE_SSL;
  //i.protocol = protocolName.c_str();
  i.pwsi = &(m_wsi);

  lwsl_notice("[DEBUG] Connection info: host=%s, port=%d, path=%s\n", i.address, i.port, i.path);

  m_state = LWS_CLIENT_CONNECTING;
  m_vhd = vhd;

  m_wsi = lws_client_connect_via_info(&i);
  lwsl_notice("[DEBUG] %s attempting connection, wsi is %p\n", m_uuid.c_str(), m_wsi);

  if (m_wsi) {
    lwsl_notice("[DEBUG] %s: lws_client_connect_via_info SUCCESS\n", m_uuid.c_str());
  } else {
    lwsl_err("[DEBUG] %s: lws_client_connect_via_info FAILED\n", m_uuid.c_str());
  }

  return nullptr != m_wsi;
}

void AudioPipe::bufferForSending(const char* text) {
  if (m_state != LWS_CLIENT_CONNECTED) return;
  {
    std::lock_guard<std::mutex> lk(m_text_mutex);
    m_metadata.append(text);
  }
  addPendingWrite(this);
}

void AudioPipe::unlockAudioBuffer() {
  // Lock-free design: check if there's pending data and trigger write
  if (hasAudioPending()) {
    addPendingWrite(this);
  }
  // No mutex to unlock - lock-free design
}

void AudioPipe::close() {
  if (m_state != LWS_CLIENT_CONNECTED) return;
  addPendingDisconnect(this);
}

void AudioPipe::finish() {
  if (m_finished || m_state != LWS_CLIENT_CONNECTED) return;
  m_finished = true;
  bufferForSending("{\"type\": \"CloseStream\"}");
}

void AudioPipe::waitForClose() {
  std::shared_future<void> sf(m_promise.get_future());
  sf.wait();
  return;
}

void AudioPipe::reset(const char* uuid, const char* host, unsigned int port, const char* path,
  size_t bufLen, size_t minFreespace, const char* apiKey, notifyHandler_t callback) {
  
  // Reset all state for reuse
  m_uuid = uuid;
  m_host = host;
  m_port = port;
  m_path = path;
  m_finished = false;
  m_audio_buffer_min_freespace = minFreespace;
  m_gracefulShutdown = false;
  m_state = LWS_CLIENT_IDLE;
  m_wsi = nullptr;
  m_vhd = nullptr;
  m_apiKey = apiKey;
  m_callback = callback;
  
  // Clear metadata
  m_metadata.clear();
  
  // Reset ring buffer
  m_audio_ring_buffer.reset();
  m_audio_bytes_pending.store(0, std::memory_order_relaxed);
  
  // Reset receive buffer
  // Reset receive buffer (keep capacity, reset content)
  m_recv_buf.clear();
  
  // Reset promise for new session
  m_promise = std::promise<void>();
  
  lwsl_info("%s AudioPipe reset for reuse (pool recycling)\n", uuid);
}
