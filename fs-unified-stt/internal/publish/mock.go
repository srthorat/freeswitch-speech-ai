package publish

import (
	"encoding/json"
	"os"
	"sync"
	"sync/atomic"
	"time"

	"google-speech-service-v2/internal/config"
	"google-speech-service-v2/internal/logx"
)

// MockPublisher logs all events to a file instead of sending to Pusher
// Used for load testing and validation

type MockPublisher struct {
	log       *logx.Logger
	file      *os.File
	mu        sync.Mutex
	
	// Stats
	eventCount    int64
	sessionStarts int64
	sessionStops  int64
	transcripts   int64
	finals        int64
}

func NewMockPublisher(cfg *config.Config, logger *logx.Logger) (*MockPublisher, error) {
	// Create log file with timestamp
	filename := "mock_pusher_events.jsonl"
	file, err := os.OpenFile(filename, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, 0644)
	if err != nil {
		return nil, err
	}
	
	logger.Info("mock pusher initialized", "log_file", filename)
	
	return &MockPublisher{
		log:  logger,
		file: file,
	}, nil
}

func (p *MockPublisher) Publish(channel, event string, data interface{}) error {
	atomic.AddInt64(&p.eventCount, 1)
	
	// Track event types
	switch event {
	case "session-started":
		atomic.AddInt64(&p.sessionStarts, 1)
	case "session-stopped":
		atomic.AddInt64(&p.sessionStops, 1)
	case "transcript-interim":
		atomic.AddInt64(&p.transcripts, 1)
	case "transcript-final":
		atomic.AddInt64(&p.transcripts, 1)
		atomic.AddInt64(&p.finals, 1)
	}
	
	// Create log entry
	entry := map[string]interface{}{
		"ts":      time.Now().UnixMilli(),
		"channel": channel,
		"event":   event,
		"data":    data,
	}
	
	p.mu.Lock()
	defer p.mu.Unlock()
	
	jsonBytes, err := json.Marshal(entry)
	if err != nil {
		return err
	}
	
	_, err = p.file.Write(append(jsonBytes, '\n'))
	return err
}

func (p *MockPublisher) Close() error {
	p.log.Info("mock pusher stats",
		"total_events", atomic.LoadInt64(&p.eventCount),
		"session_starts", atomic.LoadInt64(&p.sessionStarts),
		"session_stops", atomic.LoadInt64(&p.sessionStops),
		"transcripts", atomic.LoadInt64(&p.transcripts),
		"finals", atomic.LoadInt64(&p.finals),
	)
	
	if p.file != nil {
		return p.file.Close()
	}
	return nil
}

// GetStats returns current statistics
func (p *MockPublisher) GetStats() map[string]int64 {
	return map[string]int64{
		"total_events":   atomic.LoadInt64(&p.eventCount),
		"session_starts": atomic.LoadInt64(&p.sessionStarts),
		"session_stops":  atomic.LoadInt64(&p.sessionStops),
		"transcripts":    atomic.LoadInt64(&p.transcripts),
		"finals":         atomic.LoadInt64(&p.finals),
	}
}
