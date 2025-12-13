package audio

import "errors"

// AudioPipeline ingests PCM16 bytes from WS, resamples to 16k, and exposes
// a bounded ring buffer output for the Google sender loop.
type AudioPipeline struct {
	rb        *RingBuffer
	resampler *SpeexResampler

	inRate   int
	outRate  int
	channels int
}

type AudioPipelineConfig struct {
	InRate      int
	OutRate     int
	Channels    int
	MaxBufferMs int
}

func NewAudioPipeline(cfg AudioPipelineConfig) (*AudioPipeline, error) {
	if cfg.InRate <= 0 || cfg.OutRate <= 0 {
		return nil, errors.New("invalid sample rate")
	}
	if cfg.Channels != 1 && cfg.Channels != 2 {
		return nil, errors.New("channels must be 1 or 2")
	}
	if cfg.MaxBufferMs <= 0 {
		cfg.MaxBufferMs = 250
	}

	// buffer cap in bytes based on output rate to protect downstream latency
	bytesPerMs := (cfg.OutRate * cfg.Channels * 2) / 1000
	capBytes := bytesPerMs * cfg.MaxBufferMs

	rb, err := NewRingBuffer(capBytes)
	if err != nil {
		return nil, err
	}

	var res *SpeexResampler
	if cfg.InRate != cfg.OutRate {
		res, err = NewSpeexResampler(cfg.Channels, cfg.InRate, cfg.OutRate)
		if err != nil {
			return nil, err
		}
	}

	return &AudioPipeline{
		rb:        rb,
		resampler: res,
		inRate:    cfg.InRate,
		outRate:   cfg.OutRate,
		channels:  cfg.Channels,
	}, nil
}

func (p *AudioPipeline) Close() {
	if p.resampler != nil {
		p.resampler.Close()
		p.resampler = nil
	}
}

func (p *AudioPipeline) PushPCM16(in []byte) (overrun bool, err error) {
	if len(in)%2 != 0 {
		return false, errors.New("pcm not int16 aligned")
	}

	out := in
	if p.resampler != nil {
		out, err = p.resampler.ResamplePCM16Interleaved(in)
		if err != nil {
			return false, err
		}
	}

	overrun = p.rb.WriteDropOldest(out)
	return overrun, nil
}

func (p *AudioPipeline) Read(dst []byte) int {
	return p.rb.Read(dst)
}