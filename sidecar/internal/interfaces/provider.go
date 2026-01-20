package interfaces

import "context"

// Result represents a transcription result segment
type Result struct {
	Text       string
	Confidence float32
	IsFinal    bool
	Channel    int // 0-based channel index
}

// StreamConfig holds per-call configuration
type StreamConfig struct {
	// Identity
	UUID      string
	SIPCallID string

	// Audio Format
	SampleRate int
	Channels   int // 1 or 2

	// Recognition Settings
	Language string
	Model    string // e.g., "phone_call", "latest_long"
	Interim  bool

	// Provider Specific
	// Location is used by Google to specify the region (e.g., "us-central1")
	Location string
}

// Provider is the common interface for speech-to-text services
type Provider interface {
	// StartStream initiates a new recognition stream
	StartStream(ctx context.Context, cfg StreamConfig) (Stream, error)
}

// Stream represents an active audio stream to a provider
type Stream interface {
	// SendAudio pushes PCM audio data to the provider
	SendAudio(pcm []byte) error

	// Results returns a channel to read transcription results
	Results() <-chan Result

	// Errs returns a channel to read async errors
	Errs() <-chan error

	// Close terminates the stream
	Close()
}
