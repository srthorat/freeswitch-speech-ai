package google_v1

import (
	"context"
	"io"
	"sync"

	"google-speech-service-v2/internal/config"
	"google-speech-service-v2/internal/interfaces"
	"google-speech-service-v2/internal/logx"
	"google-speech-service-v2/internal/metrics"

	speech "cloud.google.com/go/speech/apiv1"
	speechpb "cloud.google.com/go/speech/apiv1/speechpb"
	"google.golang.org/api/option"
	"google.golang.org/grpc/status"
)

type Client struct {
	cfg *config.Config
	log *logx.Logger
	m   *metrics.Metrics
}

func NewClient(cfg *config.Config, logger *logx.Logger, m *metrics.Metrics) (*Client, error) {
	return &Client{
		cfg: cfg,
		log: logger,
		m:   m,
	}, nil
}

func (c *Client) Close() error { return nil }

type StreamSession struct {
	stream speechpb.Speech_StreamingRecognizeClient
	client *speech.Client

	results chan interfaces.Result
	errCh   chan error
	closed  chan struct{}
	once    sync.Once
}

func (c *Client) StartStream(ctx context.Context, cfg interfaces.StreamConfig) (interfaces.Stream, error) {
	// Options
	// Note: V1 endpoint template might differ or use default
	opts := []option.ClientOption{}
	// If needed override endpoint: option.WithEndpoint(...)

	client, err := speech.NewClient(ctx, opts...)
	if err != nil {
		return nil, err
	}

	stream, err := client.StreamingRecognize(ctx)
	if err != nil {
		_ = client.Close()
		return nil, err
	}

	// Calculate encoding
	encoding := speechpb.RecognitionConfig_LINEAR16
	// If 8k w/ AMR? usually LINEAR16 is safest for raw PCM

	recCfg := &speechpb.RecognitionConfig{
		Encoding:        encoding,
		SampleRateHertz: int32(cfg.SampleRate),
		LanguageCode:    cfg.Language,
		Model:           cfg.Model,
		UseEnhanced:     true,
		EnableWordTimeOffsets: true,
		EnableWordConfidence:  true,
		// EnableAutomaticPunctuation: true, // V1 feature
		// AudioChannelCount: ... 
	}
	if cfg.Channels > 1 {
		recCfg.AudioChannelCount = int32(cfg.Channels)
		recCfg.EnableSeparateRecognitionPerChannel = true
	}

	streamCfg := &speechpb.StreamingRecognitionConfig{
		Config:         recCfg,
		InterimResults: cfg.Interim,
	}

	if err := stream.Send(&speechpb.StreamingRecognizeRequest{
		StreamingRequest: &speechpb.StreamingRecognizeRequest_StreamingConfig{
			StreamingConfig: streamCfg,
		},
	}); err != nil {
		_ = stream.CloseSend()
		_ = client.Close()
		return nil, err
	}

	ss := &StreamSession{
		stream:  stream,
		client:  client,
		results: make(chan interfaces.Result, 128),
		errCh:   make(chan error, 1),
		closed:  make(chan struct{}),
	}
	
	go ss.recvLoop(c.log, cfg.UUID, cfg.SIPCallID)

	return ss, nil
}

func (s *StreamSession) SendAudio(pcm []byte) error {
	return s.stream.Send(&speechpb.StreamingRecognizeRequest{
		StreamingRequest: &speechpb.StreamingRecognizeRequest_AudioContent{
			AudioContent: pcm,
		},
	})
}

func (s *StreamSession) Results() <-chan interfaces.Result { return s.results }
func (s *StreamSession) Errs() <-chan error               { return s.errCh }

func (s *StreamSession) Close() {
	s.once.Do(func() {
		_ = s.stream.CloseSend()
		_ = s.client.Close()
		close(s.closed)
	})
}

func (s *StreamSession) recvLoop(logger *logx.Logger, uuid, sipCallID string) {
	defer close(s.results)

	for {
		resp, err := s.stream.Recv()
		if err == io.EOF {
			return
		}
		if err != nil {
			select {
			case s.errCh <- err:
			default:
			}
			st, _ := status.FromError(err)
			logger.Warn("google-v1 stream recv ended",
				"uuid", uuid,
				"sip_call_id", sipCallID,
				"err", err,
				"grpc_code", st.Code().String(),
				"grpc_msg", st.Message(),
			)
			return
		}

		for _, r := range resp.Results {
			ch := int(r.ChannelTag)
			if ch > 0 {
				ch = ch - 1
			}
			isFinal := r.IsFinal

			for _, alt := range r.Alternatives {
				if alt.Transcript == "" {
					continue
				}
				s.results <- interfaces.Result{
					Text:       alt.Transcript,
					Confidence: alt.Confidence,
					IsFinal:    isFinal,
					Channel:    ch,
				}
				break
			}
		}
	}
}
