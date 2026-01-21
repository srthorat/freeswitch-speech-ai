package mock

import (
	"context"
	"math/rand"
	"sync"
	"time"

	"google-speech-service-v2/internal/config"
	"google-speech-service-v2/internal/interfaces"
	"google-speech-service-v2/internal/logx"
	"google-speech-service-v2/internal/metrics"
)

// Mock provider for load testing - simulates STT responses without real API calls

type Client struct {
	cfg *config.Config
	log *logx.Logger
	m   *metrics.Metrics
}

func NewClient(cfg *config.Config, logger *logx.Logger, m *metrics.Metrics) (*Client, error) {
	logger.Info("mock provider initialized - for load testing only")
	return &Client{
		cfg: cfg,
		log: logger,
		m:   m,
	}, nil
}

func (c *Client) Close() error { return nil }

type StreamSession struct {
	results chan interfaces.Result
	errCh   chan error
	closed  chan struct{}
	once    sync.Once
	
	cancel context.CancelFunc
}

func (c *Client) StartStream(ctx context.Context, cfg interfaces.StreamConfig) (interfaces.Stream, error) {
	ctx, cancel := context.WithCancel(ctx)
	
	ss := &StreamSession{
		results: make(chan interfaces.Result, 128),
		errCh:   make(chan error, 1),
		closed:  make(chan struct{}),
		cancel:  cancel,
	}
	
	// Start mock response generator
	go ss.generateMockResponses(ctx, cfg)
	
	return ss, nil
}

func (s *StreamSession) SendAudio(pcm []byte) error {
	// Mock: Just accept audio without processing
	// In real load test, this validates we can handle audio throughput
	select {
	case <-s.closed:
		return nil
	default:
		return nil
	}
}

func (s *StreamSession) Results() <-chan interfaces.Result { return s.results }
func (s *StreamSession) Errs() <-chan error               { return s.errCh }

func (s *StreamSession) Close() {
	s.once.Do(func() {
		s.cancel()
		close(s.closed)
	})
}

// generateMockResponses simulates realistic STT response patterns
func (s *StreamSession) generateMockResponses(ctx context.Context, cfg interfaces.StreamConfig) {
	defer close(s.results)
	
	// Simulate response patterns similar to real providers
	phrases := []string{
		"hello how are you",
		"I'm doing great thanks",
		"let me check that for you",
		"one moment please",
		"thank you for calling",
		"is there anything else",
		"have a nice day",
		"goodbye",
	}
	
	ticker := time.NewTicker(500 * time.Millisecond) // Send mock result every 500ms
	defer ticker.Stop()
	
	phraseIdx := 0
	isPartial := true
	
	for {
		select {
		case <-ctx.Done():
			return
		case <-s.closed:
			return
		case <-ticker.C:
			channel := rand.Intn(2) // Random channel 0 or 1
			
			if isPartial {
				// Send partial result
				s.results <- interfaces.Result{
					Text:       phrases[phraseIdx],
					Confidence: 0.85 + rand.Float32()*0.1,
					IsFinal:    false,
					Channel:    channel,
				}
				isPartial = false
			} else {
				// Send final result
				s.results <- interfaces.Result{
					Text:       phrases[phraseIdx],
					Confidence: 0.95 + rand.Float32()*0.05,
					IsFinal:    true,
					Channel:    channel,
				}
				isPartial = true
				phraseIdx = (phraseIdx + 1) % len(phrases)
			}
		}
	}
}
