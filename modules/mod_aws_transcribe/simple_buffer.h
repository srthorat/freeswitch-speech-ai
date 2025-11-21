/**
 * (very) simple and limited circular buffer, 
 * supporting only the use case of doing all of the adds
 * and then subsquently retrieves.
 * 
 */
class SimpleBuffer {
  public:
    SimpleBuffer(uint32_t chunkSize, uint32_t numChunks) : numItems(0),
    m_numChunks(numChunks), m_chunkSize(chunkSize) {
      m_pData = new char[chunkSize * numChunks];
      m_pNextWrite = m_pData;
    }
    ~SimpleBuffer() {
      delete [] m_pData;
    }

    void add(void *data, uint32_t datalen) {
      // Accept any size audio, not just exact multiples of chunkSize
      // This prevents audio loss during pre-connection buffering
      if (datalen == 0) return;

      // Calculate how many full chunks we can extract
      int numChunks = datalen / m_chunkSize;

      // Handle full chunks
      for (int i = 0; i < numChunks; i++) {
        memcpy(m_pNextWrite, data, m_chunkSize);
        data = static_cast<char*>(data) + m_chunkSize;
        if (numItems < m_numChunks) numItems++;

        uint32_t offset = (m_pNextWrite - m_pData) / m_chunkSize;
        if (offset >= m_numChunks - 1) m_pNextWrite = m_pData;
        else m_pNextWrite += m_chunkSize;
      }

      // Handle any remaining partial chunk by padding with zeros
      uint32_t remainder = datalen % m_chunkSize;
      if (remainder > 0) {
        memcpy(m_pNextWrite, data, remainder);
        // Pad with zeros to complete the chunk
        memset(static_cast<char*>(m_pNextWrite) + remainder, 0, m_chunkSize - remainder);

        if (numItems < m_numChunks) numItems++;

        uint32_t offset = (m_pNextWrite - m_pData) / m_chunkSize;
        if (offset >= m_numChunks - 1) m_pNextWrite = m_pData;
        else m_pNextWrite += m_chunkSize;
      }
    }

    char* getNextChunk() {
      if (numItems--) {
        char *p = m_pNextWrite;
        uint32_t offset = (m_pNextWrite - m_pData) / m_chunkSize;
        if (offset >= m_numChunks - 1) m_pNextWrite = m_pData;
        else m_pNextWrite += m_chunkSize;
        return p;
      }
      return nullptr;
    }

    uint32_t getNumItems() { return numItems;}

  private:
    char *m_pData;
    uint32_t numItems;
    uint32_t m_chunkSize;
    uint32_t m_numChunks;
    char* m_pNextWrite;
};
