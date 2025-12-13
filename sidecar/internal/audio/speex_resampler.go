package audio

/*
#cgo pkg-config: speexdsp
#include <speex/speex_resampler.h>
#include <stdlib.h>
*/
import "C"
import (
	"errors"
	"unsafe"
)

type SpeexResampler struct {
	st       *C.SpeexResamplerState
	inRate   int
	outRate  int
	channels int
}

func NewSpeexResampler(channels, inRate, outRate int) (*SpeexResampler, error) {
	if channels != 1 && channels != 2 {
		return nil, errors.New("channels must be 1 or 2")
	}
	if inRate <= 0 || outRate <= 0 {
		return nil, errors.New("invalid sample rate")
	}
	var err C.int
	st := C.speex_resampler_init(C.uint(channels), C.uint(inRate), C.uint(outRate), C.int(4), &err)
	if st == nil || err != 0 {
		return nil, errors.New("speex_resampler_init failed")
	}
	return &SpeexResampler{
		st:       st,
		inRate:   inRate,
		outRate:  outRate,
		channels: channels,
	}, nil
}

func (r *SpeexResampler) Close() {
	if r.st != nil {
		C.speex_resampler_destroy(r.st)
		r.st = nil
	}
}

// ResamplePCM16Interleaved resamples interleaved PCM16.
// Input and output are []byte in little-endian int16.
func (r *SpeexResampler) ResamplePCM16Interleaved(in []byte) ([]byte, error) {
	if r.st == nil {
		return nil, errors.New("resampler closed")
	}
	if len(in)%2 != 0 {
		return nil, errors.New("input not int16 aligned")
	}
	inSamplesTotal := len(in) / 2
	if inSamplesTotal == 0 {
		return nil, nil
	}
	inFrames := inSamplesTotal / r.channels
	if inFrames == 0 {
		return nil, nil
	}

	// Worst-case output frames: scale by rate ratio + a bit.
	// speex expects frame counts in samples per channel for interleaved processing.
	ratio := float64(r.outRate) / float64(r.inRate)
	outFramesMax := int(float64(inFrames)*ratio) + 64
	if outFramesMax < 64 {
		outFramesMax = 64
	}

	out := make([]byte, outFramesMax*r.channels*2)

	inLen := C.spx_uint32_t(inFrames)
	outLen := C.spx_uint32_t(outFramesMax)

	// Cast bytes to int16 pointers
	inPtr := (*C.spx_int16_t)(unsafe.Pointer(&in[0]))
	outPtr := (*C.spx_int16_t)(unsafe.Pointer(&out[0]))

	rc := C.speex_resampler_process_interleaved_int(r.st, inPtr, &inLen, outPtr, &outLen)
	if rc != 0 {
		return nil, errors.New("speex_resampler_process_interleaved_int failed")
	}

	// outLen is frames per channel. Convert to bytes.
	writtenBytes := int(outLen) * r.channels * 2
	return out[:writtenBytes], nil
}