package wsserver

import (
	"context"
	"encoding/json"
	"errors"
	"io"
	"net/http"
	"sync"
	"time"

	"github.com/gorilla/websocket"

	"google-speech-service-v2/internal/audio"
	"google-speech-service-v2/internal/config"
	"google-speech-service-v2/internal/google"
	"google-speech-service-v2/internal/logx"
	"google-speech-service-v2/internal/metrics"
	"google-speech-service-v2/internal/publish"
)

type Dependencies struct {
	Cfg       *config.Config
	Google    *google.Client
	Publisher publish.Publisher
	Metrics   *metrics.Metrics
	Logger    *logx.Logger
}

type Server struct {
	cfg *config.Config
	g   *google.Client
	pub publish.Publisher
	m   *metrics.Metrics
	log *logx.Logger

	upgrader websocket.Upgrader

	mu       sync.Mutex
	sessions map[string]*Session // uuid -> session
}

func New(dep Dependencies) *Server {
	return &Server{
		cfg: dep.Cfg,
		g:   dep.Google,
		pub: dep.Publisher,
		m:   dep.Metrics,
		log: dep.Logger,
		upgrader: websocket.Upgrader{
			ReadBufferSize:  16 * 1024,
			WriteBufferSize: 8 * 1024,
			CheckOrigin:     func(r *http.Request) bool { return true },
		},
		sessions: make(map[string]*Session),
	}
}

type Meta struct {
	UUID       string `json:"uuid"`
	SIPCallID  string `json:"sip_call_id"`
	SampleRate int    `json:"sample_rate"`
	Channels   int    `json:"channels"`
	Lang       string `json:"lang"`
	Location   string `json:"location"`
	StereoSwap bool   `json:"stereo_swap"`

	Caller Speaker `json:"caller"`
	Callee Speaker `json:"callee"`
}

type Speaker struct {
	Name  string `json:"name"`
	Phone string `json:"phone"`
}

func (s *Server) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	conn, err := s.upgrader.Upgrade(w, r, nil)
	if err != nil {
		s.m.WSUpgradeFailed.Inc()
		return
	}
	s.m.WSConnections.Inc()
	defer func() {
		s.m.WSConnections.Dec()
		_ = conn.Close()
	}()

	// First message must be metadata text frame.
	msgType, payload, err := conn.ReadMessage()
	if err != nil {
		return
	}
	if msgType != websocket.TextMessage {
		return
	}

	var meta Meta
	if err := json.Unmarshal(payload, &meta); err != nil {
		return
	}
	if meta.UUID == "" || meta.SIPCallID == "" || meta.SampleRate <= 0 || meta.Channels <= 0 {
		return
	}
	if meta.Lang == "" {
		meta.Lang = "en-US"
	}
	location := meta.Location
	if location == "" {
		location = s.cfg.GCPDefaultLocation
	}

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	pipeline, err := audio.NewAudioPipeline(audio.AudioPipelineConfig{
		InRate:      meta.SampleRate,
		OutRate:     s.cfg.TargetSampleRate,
		Channels:    meta.Channels,
		MaxBufferMs: s.cfg.MaxBufferMs,
	})
	if err != nil {
		s.log.Error("audio pipeline init failed", "uuid", meta.UUID, "err", err)
		return
	}
	defer pipeline.Close()

	sess := NewSession(SessionDeps{
		Cfg:       s.cfg,
		Google:    s.g,
		Publisher: s.pub,
		Metrics:   s.m,
		Logger:    s.log,
		Ctx:       ctx,

		Meta:      meta,
		Location:  location,
		Pipeline:  pipeline,
	})
	if err := sess.Start(); err != nil {
		s.log.Error("session start failed", "uuid", meta.UUID, "sip_call_id", meta.SIPCallID, "err", err)
		return
	}
	defer sess.Stop()

	s.addSession(meta.UUID, sess)
	defer s.removeSession(meta.UUID)

	// Tighten WS settings for production
	conn.SetReadLimit(8 * 1024 * 1024)
	_ = conn.SetReadDeadline(time.Now().Add(60 * time.Second))
	conn.SetPongHandler(func(string) error {
		_ = conn.SetReadDeadline(time.Now().Add(60 * time.Second))
		return nil
	})

	// Read loop. Use NextReader to avoid extra allocations.
	for {
		mt, reader, err := conn.NextReader()
		if err != nil {
			if errors.Is(err, io.EOF) {
				return
			}
			return
		}

		switch mt {
		case websocket.BinaryMessage:
			// Read into a bounded temp buffer
			buf := make([]byte, 32*1024)
			for {
				n, rerr := reader.Read(buf)
				if n > 0 {
					chunk := buf[:n]
					s.m.AudioBytesIn.Add(float64(n))
					_ = sess.OnAudio(chunk) // overrun handled inside
				}
				if rerr != nil {
					break
				}
			}
		case websocket.TextMessage:
			// optional: allow stop metadata frames, DTMF, etc.
			// ignored for now
		}
	}
}

func (s *Server) addSession(uuid string, sess *Session) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.sessions[uuid] = sess
}

func (s *Server) removeSession(uuid string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	delete(s.sessions, uuid)
}