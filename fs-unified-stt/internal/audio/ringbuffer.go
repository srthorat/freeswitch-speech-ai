package audio

import (
	"errors"
	"sync"
)

// RingBuffer is a bounded byte ring buffer with drop-oldest behavior.
// Optimized for single-writer/single-reader goroutine pairs, but protected by a mutex
// for simplicity & safety under very high concurrency.
type RingBuffer struct {
	mu sync.Mutex

	buf []byte
	r   int
	w   int
	n   int
}

func NewRingBuffer(capacity int) (*RingBuffer, error) {
	if capacity <= 0 {
		return nil, errors.New("capacity must be > 0")
	}
	return &RingBuffer{buf: make([]byte, capacity)}, nil
}

func (rb *RingBuffer) Cap() int { return len(rb.buf) }
func (rb *RingBuffer) Len() int {
	rb.mu.Lock()
	defer rb.mu.Unlock()
	return rb.n
}

// WriteDropOldest writes all bytes. If insufficient space, it drops oldest bytes.
// Returns overrun=true if any bytes were dropped.
func (rb *RingBuffer) WriteDropOldest(p []byte) (overrun bool) {
	rb.mu.Lock()
	defer rb.mu.Unlock()

	if len(p) >= len(rb.buf) {
		// keep only last cap bytes
		p = p[len(p)-len(rb.buf):]
		overrun = true
		// reset buffer
		rb.r, rb.w, rb.n = 0, 0, 0
	}

	space := len(rb.buf) - rb.n
	if len(p) > space {
		// drop oldest
		drop := len(p) - space
		rb.r = (rb.r + drop) % len(rb.buf)
		rb.n -= drop
		overrun = true
	}

	// write
	for _, b := range p {
		rb.buf[rb.w] = b
		rb.w = (rb.w + 1) % len(rb.buf)
	}
	rb.n += len(p)
	return overrun
}

// Read reads up to len(dst) bytes, returns number of bytes read.
func (rb *RingBuffer) Read(dst []byte) int {
	rb.mu.Lock()
	defer rb.mu.Unlock()

	if rb.n == 0 {
		return 0
	}
	nread := len(dst)
	if nread > rb.n {
		nread = rb.n
	}
	for i := 0; i < nread; i++ {
		dst[i] = rb.buf[rb.r]
		rb.r = (rb.r + 1) % len(rb.buf)
	}
	rb.n -= nread
	return nread
}