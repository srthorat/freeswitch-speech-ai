package wsserver

import (
	"context"
	"errors"
	"time"

	"google-speech-service-v2/internal/audio"
	"google-speech-service-v2/internal/config"
	"google-speech-service-v2/internal/config"
	"google-speech-service-v2/internal/interfaces"
	"google-speech-service-v2/internal/logx"
	"google-speech-service-v2/internal/metrics"
	"google-speech-service-v2/internal/publish"
)

type SessionDeps struct {
	Cfg       *config.Config
	Provider  interfaces.Provider
	Publisher publish.Publisher
	Metrics   *metrics.Metrics
	Logger    *logx.Logger
	Ctx       context.Context

	Meta     Meta
	Location string
	Pipeline *audio.AudioPipeline
}

type Session struct {
	cfg *config.Config
	prov interfaces.Provider
	pub publish.Publisher
	m   *metrics.Metrics
	log *logx.Logger

	ctx    context.Context
	cancel context.CancelFunc

	meta     Meta
	location string

	pipeline *audio.AudioPipeline

	stream interfaces.Stream
}

func NewSession(dep SessionDeps) *Session {
	ctx, cancel := context.WithCancel(dep.Ctx)
	return &Session{
	return &Session{
		cfg:      dep.Cfg,
		prov:     dep.Provider,
		pub:      dep.Publisher,
		m:        dep.Metrics,
		log:      dep.Logger,
		ctx:      ctx,
		cancel:   cancel,
		meta:     dep.Meta,
		location: dep.Location,
		pipeline: dep.Pipeline,
	}
}

func (s *Session) Start() error {
	stt, err := s.prov.StartStream(s.ctx, interfaces.StreamConfig{
		UUID:       s.meta.UUID,
		SIPCallID:  s.meta.SIPCallID,
		Location:   s.location, 
		Language:   s.meta.Lang,
		SampleRate: s.cfg.TargetSampleRate,
		Channels:   s.meta.Channels,
		Model:      s.cfg.GCPModel, // We can rename this in config later to be generic 'Model'
		Interim:    s.cfg.GCPInterimResults,
	})
	if err != nil {
		return err
	}
	}
	s.stream = stt

	go s.senderLoop()
	go s.receiverLoop()

	s.log.Info("session started",
		"uuid", s.meta.UUID,
		"sip_call_id", s.meta.SIPCallID,
		"in_rate", s.meta.SampleRate,
		"out_rate", s.cfg.TargetSampleRate,
		"channels", s.meta.Channels,
		"lang", s.meta.Lang,
		"location", s.location,
	)
	return nil
}

func (s *Session) Stop() {
	s.cancel()
	if s.stream != nil {
		s.stream.Close()
		s.stream = nil
	}
	s.log.Info("session stopped", "uuid", s.meta.UUID, "sip_call_id", s.meta.SIPCallID)
}

func (s *Session) OnAudio(pcm []byte) error {
	if s.stream == nil {
		return errors.New("stt not started")
	}
	over, err := s.pipeline.PushPCM16(pcm)
	if err != nil {
		s.log.Warn("audio push failed", "uuid", s.meta.UUID, "err", err)
		return err
	}
	if over {
		s.m.AudioOverruns.Inc()
	}
	return nil
}

// senderLoop continuously drains the ring buffer and sends to Google.
// This keeps the WS reader path minimal (good for 1000-3000 connections).
func (s *Session) senderLoop() {
	defer s.log.Debug("senderLoop exit", "uuid", s.meta.UUID)

	buf := make([]byte, 6400) // ~100ms stereo@16k = 16k*2ch*2B*0.1=6400
	tick := time.NewTicker(20 * time.Millisecond)
	defer tick.Stop()

	for {
		select {
		case <-s.ctx.Done():
			return
		case <-tick.C:
			if s.stream == nil {
				return
			}
			n := s.pipeline.Read(buf)
			if n == 0 {
				continue
			}
			out := buf[:n]
			s.m.AudioBytesToGoogle.Add(float64(n))
			if err := s.stream.SendAudio(out); err != nil {
				s.log.Warn("SendAudio failed", "uuid", s.meta.UUID, "err", err)
				return
			}
		}
	}
}

func (s *Session) receiverLoop() {
	defer s.log.Debug("receiverLoop exit", "uuid", s.meta.UUID)

	idle := time.NewTimer(s.cfg.GoogleStreamIdleTimeout)
	defer idle.Stop()

	for {
		select {
		case <-s.ctx.Done():
			return

		case err := <-s.stream.Errs():
			// stream ended
			if err != nil {
				s.log.Warn("google stream ended", "uuid", s.meta.UUID, "err", err)
			}
			return

		case res, ok := <-s.stream.Results():
			if !ok {
				return
			}
			if !idle.Stop() {
				select { case <-idle.C: default: }
			}
			idle.Reset(s.cfg.GoogleStreamIdleTimeout)

			role, caller, callee := s.mapChannel(res.Channel)

			ev := publish.TranscriptEvent{
				UUID:      s.meta.UUID,
				SIPCallID: s.meta.SIPCallID,
				Channel:   res.Channel,
				Role:      role,
				Caller: publish.Speaker{
					Name:  caller.Name,
					Phone: caller.Phone,
				},
				Callee: publish.Speaker{
					Name:  callee.Name,
					Phone: callee.Phone,
				},
				Text:       res.Text,
				Confidence: res.Confidence,
				IsFinal:    res.IsFinal,
				TsMS:       time.Now().UnixMilli(),
			}

			eventName := "transcript-interim"
			if ev.IsFinal {
				eventName = "transcript-final"
			}
			if err := s.pub.Publish(s.meta.SIPCallID, eventName, ev); err != nil {
				s.log.Warn("pusher publish failed",
					"uuid", s.meta.UUID,
					"sip_call_id", s.meta.SIPCallID,
					"err", err,
				)
			}

		case <-idle.C:
			s.m.GoogleIdleTimeouts.Inc()
			s.log.Warn("google idle timeout; closing", "uuid", s.meta.UUID, "sip_call_id", s.meta.SIPCallID)
			s.Stop()
			return
		}
	}
}

// mapChannel maps channel -> caller/callee based on metadata + optional stereo_swap.
func (s *Session) mapChannel(ch int) (role string, caller Speaker, callee Speaker) {
	// Apply swap if requested
	if s.meta.StereoSwap {
		if ch == 0 {
			ch = 1
		} else if ch == 1 {
			ch = 0
		}
	}

	// Default: ch0=caller, ch1=callee
	if ch == 0 {
		return "caller", s.meta.Caller, s.meta.Callee
	}
	return "callee", s.meta.Caller, s.meta.Callee
}