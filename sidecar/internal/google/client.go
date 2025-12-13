package google

import (
	"context"
	"fmt"
	"sync"
	"time"

	"google-speech-service-v2/internal/config"
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

type StreamConfig struct {
	UUID       string
	SIPCallID  string
	Project    string
	Location   string
	Recognizer string // base id (without lang suffix)
	AutoCreate bool

	Lang       string
	SampleRate int
	Channels   int
	Model      string
	Interim    bool
}

type StreamSession struct {
	stream speechpb.Speech_StreamingRecognizeClient
	client *speech.Client

	results chan Result
	errCh   chan error

	closed chan struct{}
	once   sync.Once
}

type Result struct {
	Text       string
	Confidence float32
	IsFinal    bool
	Channel    int // normalized 0-based
}

func (c *Client) StartStream(ctx context.Context, sc StreamConfig) (*StreamSession, error) {
	endpoint := fmt.Sprintf(c.cfg.GoogleEndpointTemplate, sc.Location)
	client, err := speech.NewClient(ctx, option.WithEndpoint(endpoint))
	if err != nil {
		return nil, err
	}

	recognizerName, err := c.getOrCreateRecognizer(ctx, client, sc)
	if err != nil {
		_ = client.Close()
		return nil, err
	}

	stream, err := client.StreamingRecognize(ctx)
	if err != nil {
		_ = client.Close()
		return nil, err
	}

	// NOTE: channel identification fields vary; adjust if needed.
	recCfg := &speechpb.RecognitionConfig{
		LanguageCodes: []string{sc.Lang},
		DecodingConfig: &speechpb.RecognitionConfig_ExplicitDecodingConfig{
			ExplicitDecodingConfig: &speechpb.ExplicitDecodingConfig{
				AudioEncoding:      speechpb.ExplicitDecodingConfig_LINEAR16,
				SampleRateHertz:    int32(sc.SampleRate),
				AudioChannelCount:  int32(sc.Channels),
			},
		},
		Features: &speechpb.RecognitionFeatures{
			EnableAutomaticPunctuation: true,
			// If your proto supports explicit channel identification, set it here.
			// Some versions have: EnableSeparateRecognitionPerChannel: true
		},
		Model: sc.Model,
	}

	streamCfg := &speechpb.StreamingRecognitionConfig{
		Config:         recCfg,
		InterimResults: sc.Interim,
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
		results: make(chan Result, 128),
		errCh:   make(chan error, 1),
		closed:  make(chan struct{}),
	}

	go ss.recvLoop(c.log, sc.UUID, sc.SIPCallID, c.m)
	return ss, nil
}

func (s *StreamSession) Close() {
	s.once.Do(func() {
		_ = s.stream.CloseSend()
		_ = s.client.Close()
		close(s.closed)
	})
}

func (s *StreamSession) Results() <-chan Result { return s.results }
func (s *StreamSession) Errs() <-chan error     { return s.errCh }
func (s *StreamSession) Closed() <-chan struct{} { return s.closed }

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
			// propagate error once
			select {
			case s.errCh <- err:
			default:
			}
			st, _ := status.FromError(err)
			logger.Warn("google stream recv ended",
				"uuid", uuid,
				"sip_call_id", sipCallID,
				"err", err,
				"grpc_code", st.Code().String(),
				"grpc_msg", st.Message(),
			)
			return
		}

		// Parse results (proto may differ slightly)
		for _, r := range resp.Results {
			ch := int(r.ChannelTag)
			// Normalize common behavior: 1..N => 0..N-1
			if ch > 0 {
				ch = ch - 1
			}
			isFinal := r.IsFinal

			for _, alt := range r.Alternatives {
				if alt.Transcript == "" {
					continue
				}
				s.results <- Result{
					Text:       alt.Transcript,
					Confidence: alt.Confidence,
					IsFinal:    isFinal,
					Channel:    ch,
				}
				break
			}
		}

		_ = time.Now() // placeholder to keep structure stable; idle handled at session layer
		m.GoogleResponses.Inc()
	}
}

func (c *Client) getOrCreateRecognizer(ctx context.Context, client *speech.Client, sc StreamConfig) (string, error) {
	key := sc.Location + "|" + sc.Lang

	c.recMu.Lock()
	if name, ok := c.recognizers[key]; ok {
		c.recMu.Unlock()
		return name, nil
	}
	c.recMu.Unlock()

	parent := fmt.Sprintf("projects/%s/locations/%s", sc.Project, sc.Location)
	recognizerID := fmt.Sprintf("%s-%s", sc.Recognizer, sc.Lang)
	full := fmt.Sprintf("%s/recognizers/%s", parent, recognizerID)

	if !sc.AutoCreate {
		c.recMu.Lock()
		c.recognizers[key] = full
		c.recMu.Unlock()
		return full, nil
	}

	// Create; handle AlreadyExists gracefully by caching anyway.
	op, err := client.CreateRecognizer(ctx, &speechpb.CreateRecognizerRequest{
		Parent:       parent,
		RecognizerId: recognizerID,
		Recognizer: &speechpb.Recognizer{
			Name: full,
			DefaultRecognitionConfig: &speechpb.RecognitionConfig{
				LanguageCodes: []string{sc.Lang},
			},
		},
	})
	if err != nil {
		// still cache and continue if it's already exists
		c.log.Warn("CreateRecognizer failed (may already exist)",
			"recognizer", full,
			"err", err,
		)
		c.recMu.Lock()
		c.recognizers[key] = full
		c.recMu.Unlock()
		return full, nil
	}

	// wait with timeout so we don't block forever
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