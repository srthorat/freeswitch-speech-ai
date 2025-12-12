#ifndef __DG_AUDIO_PIPE_HPP__
#define __DG_AUDIO_PIPE_HPP__

#include <string>
#include <vector>
#include <mutex>
#include <future>
#include <queue>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <algorithm>

#include <libwebsockets.h>
#include "lockfree_ring_buffer.hpp"
#include "lockfree_mpsc_queue.hpp"

namespace deepgram {

// Buffer size: 64KB = ~2 seconds of 16kHz mono audio (or ~1 sec stereo)
// Power of 2 for efficient modulo operations
// Increase if you see "ring buffer full" errors
static constexpr size_t AUDIO_RING_BUFFER_SIZE = 65536;

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
  typedef void (*notifyHandler_t)(const char *sessionId, NotifyEvent_t event, const char* message, bool finished);

  struct lws_per_vhost_data {
    struct lws_context *context;
    struct lws_vhost *vhost;
    const struct lws_protocols *protocol;
  };

  static void initialize(unsigned int nThreads, int loglevel, log_emit_function logger);
  static bool deinitialize();
  static void adaptive_lws_service_thread(unsigned int nServiceThread); // Phase 1: Adaptive algorithm

  // constructor
  AudioPipe(const char* uuid, const char* host, unsigned int port, const char* path, 
    size_t bufLen, size_t minFreespace, const char* apiKey, notifyHandler_t callback);
  ~AudioPipe();  

  LwsState_t getLwsState(void) { return m_state; }
  std::string& getApiKey(void) {
    return m_apiKey;
  }
  void connect(void);
  void bufferForSending(const char* text);
  
  /* ============================================================================
   * Lock-Free Audio Buffer API (HIGH SCALE OPTIMIZATION)
   * 
   * These methods replace the old mutex-based buffer API.
   * Producer (frame callback): pushAudio()
   * Consumer (LWS callback): popAudio()
   * ============================================================================ */
  
  /**
   * Push audio data into the ring buffer (producer side - frame callback)
   * Lock-free, returns immediately
   * 
   * @param data Audio data pointer
   * @param len Number of bytes to push
   * @return Number of bytes actually pushed (may be less if buffer full)
   */
  size_t pushAudio(const void* data, size_t len) {
    size_t pushed = m_audio_ring_buffer.push(data, len);
    if (pushed > 0) {
      m_audio_bytes_pending.fetch_add(pushed, std::memory_order_relaxed);
    }
    return pushed;
  }
  
  /**
   * Pop audio data from the ring buffer (consumer side - LWS callback)
   * Lock-free, returns immediately
   * 
   * @param dest Destination buffer (must have space for LWS_PRE + max_len)
   * @param max_len Maximum bytes to pop
   * @return Number of bytes popped
   */
  size_t popAudio(void* dest, size_t max_len) {
    size_t popped = m_audio_ring_buffer.pop(dest, max_len);
    if (popped > 0) {
      m_audio_bytes_pending.fetch_sub(popped, std::memory_order_relaxed);
    }
    return popped;
  }
  
  /**
   * Get available space for writing (producer perspective)
   */
  size_t audioSpaceAvailable(void) const {
    return m_audio_ring_buffer.space_available();
  }
  
  /**
   * Get available data for reading (consumer perspective)
   */
  size_t audioDataAvailable(void) const {
    return m_audio_ring_buffer.size();
  }
  
  /**
   * Get current buffer usage (bytes used)
   */
  size_t audioSize(void) const {
    return m_audio_ring_buffer.size();
  }
  
  /**
   * Get total buffer capacity
   */
  size_t audioCapacity(void) const {
    return AUDIO_RING_BUFFER_SIZE;
  }
  
  /**
   * Check if there's pending audio data
   */
  bool hasAudioPending(void) const {
    return !m_audio_ring_buffer.empty();
  }
  
  // ============================================
  // Zero-copy API for direct buffer writes
  // ============================================
  
  /**
   * Reserve contiguous space for zero-copy write
   * Returns pointer where data can be written directly (no memcpy needed)
   * Call commitAudio() after writing to make data available to consumer
   */
  AudioRingBuffer<AUDIO_RING_BUFFER_SIZE>::ReserveResult reserveAudio(size_t requested) {
    return m_audio_ring_buffer.reserve_contiguous(requested);
  }
  
  /**
   * Commit data after zero-copy write
   * Makes written data available to the consumer
   * @param len Number of bytes actually written (must be <= reserved contiguous)
   */
  void commitAudio(size_t len) {
    m_audio_ring_buffer.commit(len);
    m_audio_bytes_pending.fetch_add(len, std::memory_order_relaxed);
  }
  
  /**
   * Get maximum contiguous bytes available for zero-copy write
   * Useful to check if wrap-around will occur
   */
  size_t maxContiguousWrite(void) const {
    return m_audio_ring_buffer.max_contiguous_write();
  }
  
  /**
   * Get reserve result for wrap-around handling
   * For advanced cases where writing spans the wrap boundary
   */
  AudioRingBuffer<AUDIO_RING_BUFFER_SIZE>::ReserveSplit reserveSplit(size_t requested) {
    return m_audio_ring_buffer.reserve_split(requested);
  }
  
  /**
   * Get direct read pointer for zero-copy read
   * @return Pointer to contiguous readable data and size
   * Note: Non-const because it may update internal cache for efficiency
   */
  std::pair<const uint8_t*, size_t> peekAudio(void) {
    return m_audio_ring_buffer.peek_contiguous();
  }
  
  /**
   * Consume data after zero-copy read
   * @param len Number of bytes consumed
   */
  void consumeAudio(size_t len) {
    m_audio_ring_buffer.consume(len);
    m_audio_bytes_pending.fetch_sub(len, std::memory_order_relaxed);
  }
  
  /**
   * Get minimum free space threshold
   */
  size_t binaryMinSpace(void) const {
    return m_audio_buffer_min_freespace;
  }
  
  /* Legacy API - kept for compatibility during transition */
  size_t binarySpaceAvailable(void) {
    return audioSpaceAvailable();
  }
  char* binaryWritePtr(void) { 
    // WARNING: This is only valid for immediate memcpy + binaryWritePtrAdd
    // Consider using pushAudio() instead
    return (char *) m_legacy_write_buffer;
  }
  void binaryWritePtrAdd(size_t len) {
    // Push the data that was written to legacy buffer
    pushAudio(m_legacy_write_buffer, len);
  }
  void binaryWritePtrResetToZero(void) {
    // No-op for ring buffer - data is consumed by pop
  }
  
  // Lock/unlock are now no-ops (lock-free design)
  void lockAudioBuffer(void) {
    // No-op - lock-free design
  }
  void unlockAudioBuffer(void) ;

  void close() ;
  void finish();
  void waitForClose();
  void setClosed() { m_promise.set_value(); }
  bool isFinished() { return m_finished;}

  /**
   * Reset AudioPipe for reuse (memory pool support)
   * Called when recycling an AudioPipe from the pool
   */
  void reset(const char* uuid, const char* host, unsigned int port, const char* path,
    size_t bufLen, size_t minFreespace, const char* apiKey, notifyHandler_t callback);

  // no default constructor or copying
  AudioPipe() = delete;
  AudioPipe(const AudioPipe&) = delete;
  void operator=(const AudioPipe&) = delete;

private:

  static int lws_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len); 
  static unsigned int nchild;
  static unsigned int numContexts;
  static std::string protocolName;
  
  // Phase 1: Performance monitoring counters
  static std::atomic<uint64_t> g_active_sessions;
  static std::atomic<uint64_t> g_operations_processed;
  
  /* ============================================================================
   * LOCK-FREE PENDING QUEUES (HIGH SCALE OPTIMIZATION)
   * 
   * Replaces mutex-guarded std::vector with lock-free MPSC queues.
   * Multiple producer threads (frame callbacks) can push concurrently,
   * single consumer thread (LWS service) processes items.
   * 
   * This eliminates ~250K mutex operations per second at 5K calls!
   * ============================================================================ */
  
  // Lock-free MPSC queues for pending operations
  // Using bounded queue with 16K capacity (supports burst of pending ops)
  static BoundedMPSCQueue<AudioPipe, 16384> pendingConnectsQueue;
  static BoundedMPSCQueue<AudioPipe, 16384> pendingDisconnectsQueue;
  static BoundedMPSCQueue<AudioPipe, 16384> pendingWritesQueue;
  
  // Minimal mutex vectors - ONLY for handshake lookups (LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER)
  // Primary operations use lock-free queues above
  static std::mutex mutex_connects;
  static std::mutex mutex_disconnects;
  static std::mutex mutex_writes;
  static std::vector<AudioPipe*> pendingConnects;
  static std::vector<AudioPipe*> pendingDisconnects;
  static std::vector<AudioPipe*> pendingWrites;
  
  // PHASE 2: Pure lock-free queue system (no fallbacks)
  
  static log_emit_function logger;

  static std::mutex mapMutex;
  static std::unordered_map<std::thread::id, bool> stopFlags;
  static std::queue<std::thread::id> threadIds;

  static AudioPipe* findAndRemovePendingConnect(struct lws *wsi);
  static AudioPipe* findPendingConnect(struct lws *wsi);
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
  unsigned int m_port;
  std::string m_path;
  std::string m_metadata;
  std::mutex m_text_mutex;
  // NOTE: m_audio_mutex removed - using lock-free ring buffer instead
  int m_sslFlags;
  struct lws *m_wsi;
  
  // Lock-free audio buffer (HIGH SCALE OPTIMIZATION)
  LockFreeRingBuffer<AUDIO_RING_BUFFER_SIZE> m_audio_ring_buffer;
  std::atomic<size_t> m_audio_bytes_pending{0};  // For statistics
  size_t m_audio_buffer_min_freespace;
  
  // Legacy write buffer for compatibility with old API
  // Used by binaryWritePtr() -> binaryWritePtrAdd() pattern
  uint8_t m_legacy_write_buffer[8192];  // Max frame size
  
  // Receive buffer (not changed - only used by consumer)
  // Zero-Malloc Receive Buffer (std::vector keeps capacity across messages)
  std::vector<uint8_t> m_recv_buf;
  struct lws_per_vhost_data* m_vhd;
  notifyHandler_t m_callback;
  log_emit_function m_logger;
  std::string m_apiKey;
  bool m_gracefulShutdown;
  bool m_finished;
  std::string m_bugname;
  std::promise<void> m_promise;
  
public:
  // MPSC queue node for lock-free pending queues
  // Public because BoundedMPSCQueue needs access
  std::atomic<AudioPipe*> mpsc_next{nullptr};
};

} // namespace deepgram
#endif
