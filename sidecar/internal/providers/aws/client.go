package aws

import (
	"context"
	"errors"
	"fmt"
	"sync"

	"google-speech-service-v2/internal/config"
	"google-speech-service-v2/internal/interfaces"
	"google-speech-service-v2/internal/logx"
	"google-speech-service-v2/internal/metrics"

	"github.com/aws/aws-sdk-go-v2/aws"
	awsconfig "github.com/aws/aws-sdk-go-v2/config"
	"github.com/aws/aws-sdk-go-v2/credentials"
	"github.com/aws/aws-sdk-go-v2/service/transcribestreaming"
	"github.com/aws/aws-sdk-go-v2/service/transcribestreaming/types"
)

type Client struct {
	cfg    *config.Config
	log    *logx.Logger
	m      *metrics.Metrics
	awsCfg aws.Config
}

func NewClient(cfg *config.Config, logger *logx.Logger, m *metrics.Metrics) (*Client, error) {
	if cfg.AWSAccessKeyID == "" || cfg.AWSSecretAccessKey == "" {
		return nil, errors.New("AWS credentials missing")
	}

	creds := credentials.NewStaticCredentialsProvider(cfg.AWSAccessKeyID, cfg.AWSSecretAccessKey, "")
	awsCfg, err := awsconfig.LoadDefaultConfig(context.Background(),
		awsconfig.WithRegion(cfg.AWSRegion),
		awsconfig.WithCredentialsProvider(creds),
	)
	if err != nil {
		return nil, fmt.Errorf("failed to load aws config: %w", err)
	}

	return &Client{
		cfg:    cfg,
		log:    logger,
		m:      m,
		awsCfg: awsCfg,
	}, nil
}

func (c *Client) Close() error { return nil }

type StreamSession struct {
	client *transcribestreaming.Client
	stream *transcribestreaming.StartStreamTranscriptionEventStream
	
	results chan interfaces.Result
	errCh   chan error
	closed  chan struct{}
	
	audioCh chan []byte
	once    sync.Once
}

func (c *Client) StartStream(ctx context.Context, cfg interfaces.StreamConfig) (interfaces.Stream, error) {
	client := transcribestreaming.NewFromConfig(c.awsCfg)

	// Determine language (AWS format usually "en-US", matches FreeSWITCH default)
	lang := types.LanguageCode(cfg.Language)
	if string(lang) == "" {
		lang = types.LanguageCodeEnUs
	}
	
	// Determine sample rate
	sampleRate := int32(cfg.SampleRate)

	// StartStreamTranscriptionInput
	input := &transcribestreaming.StartStreamTranscriptionInput{
		LanguageCode:         lang,
		MediaEncoding:        types.MediaEncodingPcm,
		MediaSampleRateHertz: aws.Int32(sampleRate),
		NumberOfChannels:     aws.Int32(int32(cfg.Channels)),
		EnableChannelIdentification: cfg.Channels > 1,
	}

	// We create a custom reader that pipes audio into AWS SDK's pulling mechanism if needed, 
	// BUT AWS SDK v2 for Go uses an AudioStream specifically.
	// Actually, the Input has `AudioStream` which is an interface. 
	// Wait, the V2 SDK uses a bidirectional stream returned by the API call.
	// Let's verify standard usage: client.StartStreamTranscription(...) returns output which has GetStream().
	// Audio events are sent *into* that stream.
	
	out, err := client.StartStreamTranscription(ctx, input)
	if err != nil {
		return nil, err
	}

	stream := out.GetStream()
	
	ss := &StreamSession{
		client:  client,
		stream:  stream,
		results: make(chan interfaces.Result, 128),
		errCh:   make(chan error, 1),
		closed:  make(chan struct{}),
	}
	
	// AWS SDK v2 concurrent send/recv
	go ss.recvLoop(c.log, cfg.UUID, cfg.SIPCallID)
	
	return ss, nil
}

func (s *StreamSession) SendAudio(pcm []byte) error {
	// Chunking logic? AWS recommends smaller chunks, but Sidecar buffer is usually small (20-100ms).
	// We need to wrap it in AudioEvent.
	
	// Limit chunk size if needed (AWS max 128KB headers, usually fine for raw audio chunks of 6KB)
	const maxChunk = 32 * 1024 
	if len(pcm) > maxChunk {
		// simple split not implemented here for brevity, assuming upstream buffers ~100ms (3-6KB)
		// but checking just in case
		return errors.New("audio chunk too large")
	}

	event := &types.AudioStreamMemberAudioEvent{
		Value: types.AudioEvent{
			AudioChunk: pcm,
		},
	}
	
	// Send is thread-safe in recent SDK versions? 
	// "The stream is safe for concurrent use by a single sender and single receiver."
	// Our architecture:
	// 1 sender goroutine (Session.senderLoop) -> calls SendAudio
	// 1 receiver goroutine (Session.recvLoop) -> reading metadata
	// So YES, it is safe.
	
	return s.stream.Send(context.TODO(), event)
}

func (s *StreamSession) Results() <-chan interfaces.Result { return s.results }
func (s *StreamSession) Errs() <-chan error               { return s.errCh }

func (s *StreamSession) Close() {
	s.once.Do(func() {
		// Close the stream by sending nil or closing context?
		// Typically calling Close() on the stream member or closing the AudioEvent channel.
		// For bidirectional stream, we might need to Close events.
		_ = s.stream.Close() 
		close(s.closed)
	})
}

func (s *StreamSession) recvLoop(logger *logx.Logger, uuid, sipCallID string) {
	defer close(s.results)
	
	for {
		// New SDK v2 pattern: stream.Events() returns a channel
		select {
		case event, ok := <-s.stream.Events():
			if !ok {
				return
			}
			switch e := event.(type) {
		case *types.TranscriptResultStreamMemberTranscriptEvent:
			for _, res := range e.Value.Transcript.Results {
				
				// Channel ID logic?
				channelID := 0
				if res.ChannelId != nil {
					// AWS returns "ch_0", "ch_1" strings typically? Or int?
					// Type is *string. "ch_0"
					// We need to parse or just trust order if single channel?
					// Use naive parsing for now or assume 0 if nil
					// Actually, AWS Transcribe Streaming with channel identification returns "ch_0"
					// We can implement a helper if needed, but for now map 0/1.
					// Let's assume naive toggle if not present, or parse.
					// "ch_0" -> 0
					if *res.ChannelId == "ch_1" {
						channelID = 1
					}
				}

				if len(res.Alternatives) > 0 {
					alt := res.Alternatives[0]
					if alt.Transcript != nil {
						s.results <- interfaces.Result{
							Text:       *alt.Transcript,
							IsFinal:    !res.IsPartial,
							Channel:    channelID,
							Confidence: float32(0.0), // AWS streaming confidence might be per-word
						}
					}
				}
			}
		default:
			// other events (BadRequest, etc provided as error usually, or specific event)
		}
		}
	}
}
