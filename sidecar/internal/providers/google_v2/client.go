package google_v2

import (
	"context"
	"fmt"
	"sync"
	"time"

	"google-speech-service-v2/internal/config"
	"google-speech-service-v2/internal/interfaces"
	"google-speech-service-v2/internal/logx"
	"google-speech-service-v2/internal/metrics"

	speech "cloud.google.com/go/speech/apiv2"
	speechpb "cloud.google.com/go/speech/apiv2/speechpb"
	"google.golang.org/api/option"
	"google.golang.org/grpc/status"
)

type Client struct {
	cfg *config.Config
	log *logx.Logger
	m   *metrics.Metrics

	recMu       sync.Mutex
	recognizers map[string]string // key=location|lang -> recognizer name
}

func NewClient(cfg *config.Config, logger *logx.Logger, m *metrics.Metrics) (*Client, error) {
	return &Client{
		cfg:         cfg,
		log:         logger,
		m:           m,
		recognizers: make(map[string]string),
	}, nil
}

func (c *Client) Close() error { return nil }

type StreamSession struct {
	stream speechpb.Speech_StreamingRecognizeClient
	client *speech.Client

	results chan interfaces.Result
	errCh   chan error

	closed chan struct{}
	once   sync.Once
}

func (c *Client) StartStream(ctx context.Context, cfg interfaces.StreamConfig) (interfaces.Stream, error) {
	endpoint := fmt.Sprintf(c.cfg.GoogleEndpointTemplate, cfg.Location)
	client, err := speech.NewClient(ctx, option.WithEndpoint(endpoint))
	if err != nil {
		return nil, err
	}

	recognizerName, err := c.getOrCreateRecognizer(ctx, client, cfg)
	if err != nil {
		_ = client.Close()
		return nil, err
	}

	stream, err := client.StreamingRecognize(ctx)
	if err != nil {
		_ = client.Close()
		return nil, err
	}

	recCfg := &speechpb.RecognitionConfig{
		LanguageCodes: []string{cfg.Language},
		DecodingConfig: &speechpb.RecognitionConfig_ExplicitDecodingConfig{
			ExplicitDecodingConfig: &speechpb.ExplicitDecodingConfig{
				Encoding:          speechpb.ExplicitDecodingConfig_LINEAR16,
				SampleRateHertz:    int32(cfg.SampleRate),
				AudioChannelCount:  int32(cfg.Channels),
			},
		},
		Features: &speechpb.RecognitionFeatures{
			EnableAutomaticPunctuation: true,
		},
		Model: cfg.Model,
	}

	streamCfg := &speechpb.StreamingRecognitionConfig{
		Config:         recCfg,
		StreamingFeatures: &speechpb.StreamingRecognitionFeatures{
			InterimResults: cfg.Interim,
		},
	}

	// First request is config
	if err := stream.Send(&speechpb.StreamingRecognizeRequest{
		Recognizer: recognizerName,
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

	go ss.recvLoop(c.log, cfg.UUID, cfg.SIPCallID, c.m)
	return ss, nil
}

func (s *StreamSession) Close() {
	s.once.Do(func() {
		_ = s.stream.CloseSend()
		_ = s.client.Close()
		close(s.closed)
	})
}

func (s *StreamSession) Results() <-chan interfaces.Result { return s.results }
func (s *StreamSession) Errs() <-chan error               { return s.errCh }
// Note: Errs() is not part of the interface yet? 
// The interface definition I wrote earlier didn't have Errs(). 'SendAudio' returns error. 'Results' returns result.
// But standard usage usually needs a way to detect stream closure/error from the read side.
// Let's check wsserver usage again. 'case err := <-s.stt.Errs():'
// So I SHOULD add Errs() to the interface, or rely on Results() closing or returning error value?
// The interface I defined in step 82:
//   Results() <-chan Result
//   SendAudio() error
//   Close()
// It missed `Errs() <-chan error`. 
// However, the `recvLoop` closes `results` channel when it ends. 
// A robust interface usually has a way to signal WHY it ended (EOF vs Error).
// The existing `google/client.go` had `Errs()`.
// I should update the interface in `interfaces/provider.go` to include `Errs() <-chan error` OR
// I can just rely on logging for now, but `wsserver` explicitly waits on `stt.Errs()`.
// So I MUST Update the interface first or now.

func (s *StreamSession) SendAudio(pcm []byte) error {
	return s.stream.Send(&speechpb.StreamingRecognizeRequest{
		StreamingRequest: &speechpb.StreamingRecognizeRequest_Audio{
			Audio: pcm,
		},
	})
}

func (s *StreamSession) recvLoop(logger *logx.Logger, uuid, sipCallID string, m *metrics.Metrics) {
	defer close(s.results)

	for {
		resp, err := s.stream.Recv()
		if err != nil {
			select {
			case s.errCh <- err:
			default:
			}
			st, _ := status.FromError(err)
			logger.Warn("google-v2 stream recv ended",
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

		_ = time.Now()
		m.GoogleResponses.Inc()
	}
}

func (c *Client) getOrCreateRecognizer(ctx context.Context, client *speech.Client, cfg interfaces.StreamConfig) (string, error) {
	key := cfg.Location + "|" + cfg.Language

	c.recMu.Lock()
	if name, ok := c.recognizers[key]; ok {
		c.recMu.Unlock()
		return name, nil
	}
	c.recMu.Unlock()

	parent := fmt.Sprintf("projects/%s/locations/%s", c.cfg.GCPProject, cfg.Location)
	recognizerNameBase := c.cfg.GCPRecognizerID 
    // Wait, google/client.go had `sc.Project` and `sc.Recognizer` passed in config.
    // In my interface config, I didn't put Project/RecognizerID, assuming they are global config or passed in model?
    // In interface, I have `Model` string.
    // `google/client.go` had `Recognizer` in `StreamConfig`.
    // I should probably use `c.cfg.GCPRecognizerID` from global config for now, as that's what `wsserver` was doing roughly (it was passing s.cfg.GCPRecognizerID into the struct).
    
	recognizerID := fmt.Sprintf("%s-%s", recognizerNameBase, cfg.Language)
	full := fmt.Sprintf("%s/recognizers/%s", parent, recognizerID)

	// We assume auto-create is enabled or handled by config
	if !c.cfg.GCPEnableAutoCreate {
		c.recMu.Lock()
		c.recognizers[key] = full
		c.recMu.Unlock()
		return full, nil
	}

	op, err := client.CreateRecognizer(ctx, &speechpb.CreateRecognizerRequest{
		Parent:       parent,
		RecognizerId: recognizerID,
		Recognizer: &speechpb.Recognizer{
			Name: full,
			DefaultRecognitionConfig: &speechpb.RecognitionConfig{
				LanguageCodes: []string{cfg.Language},
			},
		},
	})
	if err != nil {
		c.log.Warn("CreateRecognizer failed (may already exist)",
			"recognizer", full,
			"err", err,
		)
		c.recMu.Lock()
		c.recognizers[key] = full
		c.recMu.Unlock()
		return full, nil
	}

	waitCtx, cancel := context.WithTimeout(ctx, 30*time.Second)
	defer cancel()
	_, werr := op.Wait(waitCtx)
	if werr != nil {
		c.log.Warn("CreateRecognizer wait failed",
			"recognizer", full,
			"err", werr,
		)
	}

	c.recMu.Lock()
	c.recognizers[key] = full
	c.recMu.Unlock()
	return full, nil
}
