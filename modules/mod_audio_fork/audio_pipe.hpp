#ifndef __AUDIO_PIPE_HPP__
#define __AUDIO_PIPE_HPP__

#include <string>
#include <list>
#include <mutex>
#include <queue>
#include <unordered_map>
#include <thread>

#include <libwebsockets.h>
#include "lockfree_ring_buffer.hpp"
#include "lockfree_mpsc_queue.hpp"
#include <atomic>
#include <chrono>
#include <vector>

// Forward declaration
class AudioPipe;

// Thread-local LWS context manager (Phase 1 optimization)
class LwsContextManager {
public:
  static struct lws_context* getContext();
  static uint32_t getContextCount() { return g_context_count.load(std::memory_order_relaxed); }
  static void shutdown();
  
  static void registerContext(struct lws_context* context);
  static void unregisterContext(struct lws_context* context);
  static void signalAllContexts();

private:
  static struct lws_context* createContext();
  thread_local static struct lws_context* t_context;
  static std::atomic<uint32_t> g_context_count;
  static std::vector<struct lws_context*> g_all_contexts;
  static std::mutex g_all_contexts_mutex;
  
  friend class AudioPipe; // Allow access to AudioPipe's private members
};

class AudioPipe {
public:
  enum LwsState_t {
    LWS_CLIENT_IDLE,
    LWS_CLIENT_CONNECTING,
    LWS_CLIENT_CONNECTED,
    LWS_CLIENT_FAILED,
    LWS_CLIENT_DISCONNECTING,
    LWS_CLIENT_DISCONNECTED
  };
  enum NotifyEvent_t {
    CONNECT_SUCCESS,
    CONNECT_FAIL,
    CONNECTION_DROPPED,
    CONNECTION_CLOSED_GRACEFULLY,
    MESSAGE
  };
  typedef void (*log_emit_function)(int level, const char *line);
  typedef void (*notifyHandler_t)(const char *sessionId, const char* bugname, NotifyEvent_t event, const char* message);

  struct lws_per_vhost_data {
    struct lws_context *context;
    struct lws_vhost *vhost;
    const struct lws_protocols *protocol;
  };

  static void initialize(const char* protocolName, unsigned int nServiceThreads, int logLevel, log_emit_function logger);
  static bool deinitialize();
  static void adaptive_lws_service_thread(unsigned int nServiceThread); // Phase 1: Adaptive algorithm

  // constructor
  AudioPipe(const char* uuid, const char* host, unsigned int port, const char* path, int sslFlags, 
    size_t bufLen, size_t minFreespace, const char* username, const char* password, char* bugname, notifyHandler_t callback);
  ~AudioPipe();

  // Reference Counting
  void retain() {
      m_refCount.fetch_add(1, std::memory_order_relaxed);
  }
  void release() {
      if (m_refCount.fetch_sub(1, std::memory_order_release) == 1) {
          std::atomic_thread_fence(std::memory_order_acquire);
          delete this;
      }
  }

  LwsState_t getLwsState(void) { return m_state; }
  void connect(void);
  void bufferForSending(const char* text);
  
  // Lock-free ring buffer API (Phase 1)
  size_t getAudioDataAvailable(void) {
    return m_audio_buffer.size();
  }
  
  // Lock-free ring buffer status API
  size_t getAvailableSpace() {
    return m_audio_buffer.space_available();
  }
  
  size_t getBufferCapacity() {
    return m_audio_buffer.capacity();
  }
  
  // Zero-copy write for producer (frame callback)
  bool reserveAudioSpace(size_t requested_len, uint8_t** ptr1, size_t* len1, uint8_t** ptr2, size_t* len2) {
    return m_audio_buffer.reserve_write(requested_len, ptr1, len1, ptr2, len2);
  }
  
  void commitAudioData(size_t len) {
    m_audio_buffer.commit_write(len);
    if (len > 0) addPendingWrite(this);
  }
  
  // Fallback: traditional push (for wrap-around cases)
  size_t pushAudio(const void* data, size_t len) {
    return m_audio_buffer.push(data, len);
  }
  
  // Direct write for immediate transmission (bypass ring buffer)
  void writeAudioFrame(const void* data, size_t len);
  
  // Consumer API (LWS thread)
  std::pair<const uint8_t*, size_t> peekAudioContiguous() {
    return m_audio_buffer.peek_contiguous();
  }
  
  void consumeAudio(size_t len) {
    m_audio_buffer.consume(len);
  }
  bool hasBasicAuth(void) {
    return !m_username.empty() && !m_password.empty();
  }

  void getBasicAuth(std::string& username, std::string& password) {
    username = m_username;
    password = m_password;
  }

  void do_graceful_shutdown();
  bool isGracefulShutdown(void) {
    return m_gracefulShutdown;
  }

  void close() ;

  // no default constructor or copying
  AudioPipe() = delete;
  AudioPipe(const AudioPipe&) = delete;
  void operator=(const AudioPipe&) = delete;

private:

  static unsigned int nchild;
  static unsigned int numContexts;
public:
  static std::string protocolName;
  static int lws_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len);
private:
  
  // Phase 1: Lock-free pending operations with atomic counters
  static std::mutex g_pending_mutex;
  static BoundedMPSCQueue<AudioPipe, 16384> pendingConnectsQueue;
  static BoundedMPSCQueue<AudioPipe, 16384> pendingDisconnectsQueue;
  static BoundedMPSCQueue<AudioPipe, 16384> pendingWritesQueue;
  static std::atomic<uint64_t> g_active_sessions;
  static std::atomic<uint64_t> g_operations_processed;
  
  // Pending connections lookup (Deepgram-style pattern)
  static std::list<AudioPipe*> pendingConnects;
  static std::mutex mutex_connects;
  static AudioPipe* findPendingConnect(struct lws* wsi);
  static AudioPipe* findAndRemovePendingConnect(struct lws* wsi);
  
  static log_emit_function logger;
  static std::atomic<bool> stopFlags;

  // Phase 1: Lock-free pending operations
  static void addPendingConnect(AudioPipe* ap);
  static void addPendingDisconnect(AudioPipe* ap);
  static void addPendingWrite(AudioPipe* ap);
  static void processPendingConnects(lws_per_vhost_data *vhd);
  static void processPendingDisconnects(lws_per_vhost_data *vhd);
  static void processPendingWrites(void);
  
  bool connect_client(struct lws_per_vhost_data *vhd);

  LwsState_t m_state;
  std::string m_uuid;
  std::string m_host;
  std::string m_bugname;
  unsigned int m_port;
  std::string m_path;
  std::string m_metadata;
  std::mutex m_text_mutex;
  int m_sslFlags;
  struct lws *m_wsi;
  
  // Phase 1: Lock-free ring buffer (replaces mutex + malloc buffer)
  // 8KB capacity = 0.25 seconds at 8kHz stereo (minimal buffering for real-time)
  // Very small buffer ensures low latency - old audio drops if network can't keep up
  static constexpr size_t RING_BUFFER_CAPACITY = 8 * 1024;
  audiofork::LockFreeRingBuffer<RING_BUFFER_CAPACITY> m_audio_buffer;
  size_t m_audio_buffer_min_freespace;
  
  uint8_t* m_recv_buf;
  uint8_t* m_recv_buf_ptr;
  size_t m_recv_buf_len;
  struct lws_per_vhost_data* m_vhd;
  static thread_local struct lws_per_vhost_data* t_vhd;
  notifyHandler_t m_callback;
  log_emit_function m_logger;

  std::string m_username;
  std::string m_password;
  bool m_gracefulShutdown;
  std::atomic<int> m_refCount;
};


#endif
