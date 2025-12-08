#include "audio_pipe.hpp"

#include <cassert>
#include <iostream>
#include <chrono>
#include <thread>

// Thread-local LWS context manager (Phase 1 optimization)
thread_local struct lws_context* LwsContextManager::t_context = nullptr;
std::atomic<uint32_t> LwsContextManager::g_context_count{0};

struct lws_context* LwsContextManager::getContext() {
    if (!t_context) {
        t_context = createContext();
        if (t_context) {
            g_context_count.fetch_add(1, std::memory_order_relaxed);
        }
    }
    return t_context;
}

struct lws_context* LwsContextManager::createContext() {
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    
    static const struct lws_protocols protocols[] = {
        { AudioPipe::protocolName.c_str(), AudioPipe::lws_callback, sizeof(AudioPipe*), 8192, 0, NULL, 8192 },
        { NULL, NULL, 0, 0, 0, NULL, 0 }
    };
    
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info.ssl_cert_filepath = NULL;
    info.ssl_private_key_filepath = NULL;
    
    return lws_create_context(&info);
}

void LwsContextManager::shutdown() {
    if (t_context) {
        lws_context_destroy(t_context);
        t_context = nullptr;
        g_context_count.fetch_sub(1, std::memory_order_relaxed);
    }
}

/* discard incoming text messages over the socket that are longer than this */
#define MAX_RECV_BUF_SIZE (65 * 1024 * 10)
#define RECV_BUF_REALLOC_SIZE (8 * 1024)


// Static member initialization (Phase 1)
BoundedMPSCQueue<AudioPipe, 16384> AudioPipe::pendingConnectsQueue;
BoundedMPSCQueue<AudioPipe, 16384> AudioPipe::pendingDisconnectsQueue;
BoundedMPSCQueue<AudioPipe, 16384> AudioPipe::pendingWritesQueue;
std::atomic<uint64_t> AudioPipe::g_active_sessions{0};
std::atomic<uint64_t> AudioPipe::g_operations_processed{0};
std::atomic<bool> AudioPipe::stopFlags{false};
AudioPipe::log_emit_function AudioPipe::logger = nullptr;
std::string AudioPipe::protocolName;
unsigned int AudioPipe::numContexts = 0;
unsigned int AudioPipe::nchild = 0;

namespace {
  static const char* basicAuthUser = std::getenv("MOD_AUDIO_FORK_HTTP_AUTH_USER");
  static const char* basicAuthPassword = std::getenv("MOD_AUDIO_FORK_HTTP_AUTH_PASSWORD");

  static const char *requestedTcpKeepaliveSecs = std::getenv("MOD_AUDIO_FORK_TCP_KEEPALIVE_SECS");
  static int nTcpKeepaliveSecs = requestedTcpKeepaliveSecs ? ::atoi(requestedTcpKeepaliveSecs) : 55;
}

// remove once we update to lws with this helper
static int dch_lws_http_basic_auth_gen(const char *user, const char *pw, char *buf, size_t len) {
	size_t n = strlen(user), m = strlen(pw);
	char b[128];

	if (len < 6 + ((4 * (n + m + 1)) / 3) + 1)
		return 1;

	memcpy(buf, "Basic ", 6);

	n = lws_snprintf(b, sizeof(b), "%s:%s", user, pw);
	if (n >= sizeof(b) - 2)
		return 2;

	lws_b64_encode_string(b, n, buf + 6, len - 6);
	buf[len - 1] = '\0';

	return 0;
}

int AudioPipe::lws_callback(struct lws *wsi, 
  enum lws_callback_reasons reason,
  void *user, void *in, size_t len) {

  struct AudioPipe::lws_per_vhost_data *vhd = 
    (struct AudioPipe::lws_per_vhost_data *) lws_protocol_vh_priv_get(lws_get_vhost(wsi), lws_get_protocol(wsi));

  struct lws_vhost* vhost = lws_get_vhost(wsi);
  AudioPipe ** ppAp = (AudioPipe **) user;

  switch (reason) {
    case LWS_CALLBACK_PROTOCOL_INIT:
      vhd = (struct AudioPipe::lws_per_vhost_data *) lws_protocol_vh_priv_zalloc(lws_get_vhost(wsi), lws_get_protocol(wsi), sizeof(struct AudioPipe::lws_per_vhost_data));
      vhd->context = lws_get_context(wsi);
      vhd->protocol = lws_get_protocol(wsi);
      vhd->vhost = lws_get_vhost(wsi);
      break;

    case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER:
      {
        // Phase 1: Direct lookup through user data (no mutex needed)
        AudioPipe* ap = *ppAp;
        if (ap && ap->hasBasicAuth()) {
          unsigned char **p = (unsigned char **)in, *end = (*p) + len;
          char b[128];
          std::string username, password;

          ap->getBasicAuth(username, password);
          lwsl_notice("AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER username: %s, password: xxxxxx\n", username.c_str());
          if (dch_lws_http_basic_auth_gen(username.c_str(), password.c_str(), b, sizeof(b))) break;
          if (lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_AUTHORIZATION, (unsigned char *)b, strlen(b), p, end)) return -1;
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
        AudioPipe* ap = *ppAp;
        int rc = lws_http_client_http_response(wsi);
        lwsl_err("AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_CONNECTION_ERROR: %s, response status %d\n", in ? (char *)in : "(null)", rc); 
        if (ap) {
          ap->m_state = LWS_CLIENT_FAILED;
          ap->m_callback(ap->m_uuid.c_str(), ap->m_bugname.c_str(), AudioPipe::CONNECT_FAIL, (char *) in);
          g_active_sessions.fetch_sub(1, std::memory_order_relaxed);
        }
        else {
          lwsl_err("AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_CONNECTION_ERROR unable to find wsi %p..\n", wsi); 
        }
      }      
      break;

    case LWS_CALLBACK_CLIENT_ESTABLISHED:
      {
        AudioPipe* ap = *ppAp;
        if (ap) {
          ap->m_vhd = vhd;
          ap->m_state = LWS_CLIENT_CONNECTED;
          ap->m_callback(ap->m_uuid.c_str(), ap->m_bugname.c_str(), AudioPipe::CONNECT_SUCCESS, NULL);
          g_active_sessions.fetch_add(1, std::memory_order_relaxed);
          
          // Trigger initial write callback to start draining audio buffer
          lws_callback_on_writable(wsi);
          // Wake up the service thread to process the writable callback immediately
          lws_cancel_service(vhd->context);
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
          ap->m_callback(ap->m_uuid.c_str(), ap->m_bugname.c_str(), AudioPipe::CONNECTION_CLOSED_GRACEFULLY, NULL);
        }
        else if (ap->m_state == LWS_CLIENT_CONNECTED) {
          // closed by far end
          lwsl_notice("%s socket closed by far end\n", ap->m_uuid.c_str());
          ap->m_callback(ap->m_uuid.c_str(), ap->m_bugname.c_str(), AudioPipe::CONNECTION_DROPPED, NULL);
        }
        ap->m_state = LWS_CLIENT_DISCONNECTED;

        //NB: after receiving any of the events above, any holder of a 
        //pointer or reference to this object must treat is as no longer valid

        *ppAp = NULL;
        delete ap;
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
          // allocate a buffer for the entire chunk of memory needed
          assert(nullptr == ap->m_recv_buf);
          ap->m_recv_buf_len = len + lws_remaining_packet_payload(wsi);
          ap->m_recv_buf = (uint8_t*) malloc(ap->m_recv_buf_len);
          ap->m_recv_buf_ptr = ap->m_recv_buf;
        }

        size_t write_offset = ap->m_recv_buf_ptr - ap->m_recv_buf;
        size_t remaining_space = ap->m_recv_buf_len - write_offset;
        if (remaining_space < len) {
          lwsl_notice("AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_RECEIVE buffer realloc needed.\n");
          size_t newlen = ap->m_recv_buf_len + RECV_BUF_REALLOC_SIZE;
          if (newlen > MAX_RECV_BUF_SIZE) {
            free(ap->m_recv_buf);
            ap->m_recv_buf = ap->m_recv_buf_ptr = nullptr;
            ap->m_recv_buf_len = 0;
            lwsl_notice("AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_RECEIVE max buffer exceeded, truncating message.\n");
          }
          else {
            ap->m_recv_buf = (uint8_t*) realloc(ap->m_recv_buf, newlen);
            if (nullptr != ap->m_recv_buf) {
              ap->m_recv_buf_len = newlen;
              ap->m_recv_buf_ptr = ap->m_recv_buf + write_offset;
            }
          }
        }

        if (nullptr != ap->m_recv_buf) {
          if (len > 0) {
            memcpy(ap->m_recv_buf_ptr, in, len);
            ap->m_recv_buf_ptr += len;
          }
          if (lws_is_final_fragment(wsi)) {
            if (nullptr != ap->m_recv_buf) {
              std::string msg((char *)ap->m_recv_buf, ap->m_recv_buf_ptr - ap->m_recv_buf);
              ap->m_callback(ap->m_uuid.c_str(), ap->m_bugname.c_str(), AudioPipe::MESSAGE, msg.c_str());
              if (nullptr != ap->m_recv_buf) free(ap->m_recv_buf);
            }
            ap->m_recv_buf = ap->m_recv_buf_ptr = nullptr;
            ap->m_recv_buf_len = 0;
          }
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

        // check for graceful close - send a zero length binary frame
        if (ap->isGracefulShutdown()) {
          lwsl_notice("%s graceful shutdown - sending zero length binary frame to flush any final responses\n", ap->m_uuid.c_str());
          uint8_t buf[LWS_PRE];
          int sent = lws_write(wsi, buf + LWS_PRE, 0, LWS_WRITE_BINARY);
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

        // Phase 1: Lock-free audio read using peek/consume pattern
        // Send all contiguous data at once for efficiency (zero-copy design)
        {
          auto [data_ptr, data_len] = ap->peekAudioContiguous();
          if (data_len > 0) {
            // Allocate buffer with LWS_PRE space
            uint8_t buf[data_len + LWS_PRE];
            memcpy(buf + LWS_PRE, data_ptr, data_len);
            
            int sent = lws_write(wsi, buf + LWS_PRE, data_len, LWS_WRITE_BINARY);
            if (sent < (int)data_len) {
              lwsl_err("AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_WRITEABLE %s attempted to send %zu only sent %d wsi %p..\n", 
                ap->m_uuid.c_str(), data_len, sent, wsi); 
            }
            
            // Consume the data we just sent
            ap->consumeAudio(data_len);
            
            // Request another callback if there's more data
            // This ensures continuous draining of the ring buffer
            if (ap->getAudioDataAvailable() > 0) {
              lws_callback_on_writable(wsi);
              // Wake up service thread for immediate processing
              lws_cancel_service(ap->m_vhd->context);
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

// Phase 1: Old static members removed - using lock-free alternatives declared earlier

void AudioPipe::processPendingConnects(lws_per_vhost_data *vhd) {
  AudioPipe* ap;
  while ((ap = pendingConnectsQueue.pop()) != nullptr) {
    if (ap->m_state == LWS_CLIENT_IDLE) {
      ap->m_state = LWS_CLIENT_CONNECTING;
      ap->connect_client(vhd);
    }
  }
}

void AudioPipe::processPendingDisconnects(lws_per_vhost_data *vhd) {
  AudioPipe* ap;
  while ((ap = pendingDisconnectsQueue.pop()) != nullptr) {
    if (ap->m_state == LWS_CLIENT_DISCONNECTING) {
      lws_callback_on_writable(ap->m_wsi); 
    }
  }
}

void AudioPipe::processPendingWrites() {
  AudioPipe* ap;
  while ((ap = pendingWritesQueue.pop()) != nullptr) {
    if (ap->m_state == LWS_CLIENT_CONNECTED) {
      lws_callback_on_writable(ap->m_wsi);
    }
  }
}

// Note: findAndRemovePendingConnect and findPendingConnect functions 
// are no longer needed with lock-free MPSC queue approach
// Session management is now handled through direct state checking

void AudioPipe::addPendingConnect(AudioPipe* ap) {
  if (pendingConnectsQueue.push(ap)) {
    lwsl_notice("%s added to pending connects queue\n", ap->m_uuid.c_str());
  } else {
    lwsl_err("%s failed to add to pending connects queue (full)\n", ap->m_uuid.c_str());
  }
  
  struct lws_context* context = LwsContextManager::getContext();
  if (context) {
    lws_cancel_service(context);
  }
}
void AudioPipe::addPendingDisconnect(AudioPipe* ap) {
  ap->m_state = LWS_CLIENT_DISCONNECTING;
  if (pendingDisconnectsQueue.push(ap)) {
    lwsl_notice("%s added to pending disconnects queue\n", ap->m_uuid.c_str());
  } else {
    lwsl_err("%s failed to add to pending disconnects queue (full)\n", ap->m_uuid.c_str());
  }
  
  if (ap->m_vhd && ap->m_vhd->context) {
    lws_cancel_service(ap->m_vhd->context);
  }
}
void AudioPipe::addPendingWrite(AudioPipe* ap) {
  // Fast path: if already connected, request write callback immediately
  // This eliminates queueing delay for sub-millisecond latency
  if (ap->m_state == LWS_CLIENT_CONNECTED && ap->m_wsi) {
    lws_callback_on_writable(ap->m_wsi);
    if (ap->m_vhd && ap->m_vhd->context) {
      lws_cancel_service(ap->m_vhd->context);
    }
  }
  else {
    // Slow path: queue for later processing
    if (!pendingWritesQueue.push(ap)) {
      lwsl_err("%s failed to add to pending writes queue (full)\n", ap->m_uuid.c_str());
    }
    if (ap->m_vhd && ap->m_vhd->context) {
      lws_cancel_service(ap->m_vhd->context);
    }
  }
}

// Phase 1: Adaptive service thread with thread-local context and exponential backoff
void AudioPipe::adaptive_lws_service_thread(unsigned int nServiceThread) {
  lwsl_notice("AudioPipe::adaptive_service_thread starting thread %d\n", nServiceThread);
  
  // Thread-local context eliminates mutex contention
  struct lws_context* context = LwsContextManager::getContext();
  if (!context) {
    lwsl_err("AudioPipe::adaptive_service_thread failed creating context in thread %d\n", nServiceThread);
    return;
  }
  
  uint32_t empty_loops = 0;
  const uint32_t max_backoff = 1000; // Max 1ms backoff
  
  while (!stopFlags.load(std::memory_order_relaxed) && context) {
    // Non-blocking LWS service (Phase 1 optimization)
    int work_done = lws_service(context, 0);
    
    // Increment operations counter
    g_operations_processed.fetch_add(1, std::memory_order_relaxed);
    
    if (work_done <= 0) {
      // Adaptive backoff: 10μs → 12μs → 14μs ... → 1ms (Phase 1)
      uint32_t delay = std::min(10 + empty_loops * 2, max_backoff);
      std::this_thread::sleep_for(std::chrono::microseconds(delay));
      empty_loops++;
    } else {
      empty_loops = 0; // Reset immediately on activity
    }
  }
  
  lwsl_notice("AudioPipe::adaptive_service_thread ending thread %d\n", nServiceThread);
}

void AudioPipe::initialize(const char* protocol, unsigned int nThreads, int loglevel, log_emit_function logger) {
  assert(nThreads > 0 && nThreads <= 10);

  numContexts = nThreads;
  protocolName = protocol;
  lws_set_log_level(loglevel, logger);
  stopFlags.store(false, std::memory_order_relaxed);

  lwsl_notice("AudioPipe::initialize starting %d threads with subprotocol %s\n", nThreads, protocol); 
  for (unsigned int i = 0; i < numContexts; i++) {
    std::thread t(&AudioPipe::adaptive_lws_service_thread, i);
    t.detach();
  }
}

bool AudioPipe::deinitialize() {
  lwsl_notice("AudioPipe::deinitialize\n"); 
  
  // Signal all threads to stop
  stopFlags.store(true, std::memory_order_relaxed);
  
  // Wait for threads to finish (they will detect stopFlags and exit)
  std::this_thread::sleep_for(std::chrono::seconds(2));
  
  // Shutdown thread-local contexts (will be called by each thread as it exits)
  LwsContextManager::shutdown();
  
  lwsl_notice("AudioPipe::deinitialize complete\n");
  return true;
}

// instance members
AudioPipe::AudioPipe(const char* uuid, const char* host, unsigned int port, const char* path,
  int sslFlags, size_t bufLen, size_t minFreespace, const char* username, const char* password, char* bugname, notifyHandler_t callback) :
  m_uuid(uuid), m_host(host), m_port(port), m_path(path), m_sslFlags(sslFlags),
  m_audio_buffer_min_freespace(minFreespace), m_gracefulShutdown(false),
  m_recv_buf(nullptr), m_recv_buf_ptr(nullptr), m_bugname(bugname),
  m_state(LWS_CLIENT_IDLE), m_wsi(nullptr), m_vhd(nullptr), m_callback(callback) {

  if (username && password) {
    m_username.assign(username);
    m_password.assign(password);
  }

  // Phase 1: Ring buffer is already initialized by member initializer
  // No malloc needed - lock-free ring buffer is stack-allocated
}
AudioPipe::~AudioPipe() {
  // Phase 1: No need to delete audio_buffer (it's not heap-allocated)
  if (m_recv_buf) delete [] m_recv_buf;
}

void AudioPipe::connect(void) {
  addPendingConnect(this);
}

bool AudioPipe::connect_client(struct lws_per_vhost_data *vhd) {
  // Phase 1: No malloc buffer assertion needed (ring buffer is stack-allocated)
  assert(m_vhd == nullptr);

  struct lws_client_connect_info i;

  memset(&i, 0, sizeof(i));
  i.context = vhd->context;
  i.port = m_port;
  i.address = m_host.c_str();
  i.path = m_path.c_str();
  i.host = i.address;
  i.origin = i.address;
  i.ssl_connection = m_sslFlags;
  i.protocol = protocolName.c_str();
  i.pwsi = &(m_wsi);

  m_state = LWS_CLIENT_CONNECTING;
  m_vhd = vhd;

  m_wsi = lws_client_connect_via_info(&i);
  lwsl_notice("%s attempting connection, wsi is %p\n", m_uuid.c_str(), m_wsi);

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

// Direct write bypassing ring buffer for immediate transmission
void AudioPipe::writeAudioFrame(const void* data, size_t len) {
  if (m_state != LWS_CLIENT_CONNECTED || !data || len == 0) return;
  
  // Write directly to ring buffer (will be sent immediately by next writable callback)
  size_t written = m_audio_buffer.push(data, len);
  if (written > 0) {
    // Trigger immediate send
    addPendingWrite(this);
  }
}

void AudioPipe::close() {
  if (m_state != LWS_CLIENT_CONNECTED) return;
  addPendingDisconnect(this);
}

void AudioPipe::do_graceful_shutdown() {
  m_gracefulShutdown = true;
  addPendingWrite(this);
}
