package metrics

import (
	"net/http"

	"github.com/prometheus/client_golang/prometheus"
	"github.com/prometheus/client_golang/prometheus/promhttp"
)

type Metrics struct {
	reg *prometheus.Registry

	WSConnections   prometheus.Gauge
	WSUpgradeFailed prometheus.Counter

	AudioBytesIn       prometheus.Counter
	AudioBytesToGoogle prometheus.Counter
	AudioOverruns      prometheus.Counter

	GoogleResponses    prometheus.Counter
	GoogleIdleTimeouts prometheus.Counter
}

func New() *Metrics {
	reg := prometheus.NewRegistry()

	m := &Metrics{
		reg: reg,
		WSConnections: prometheus.NewGauge(prometheus.GaugeOpts{
		Name: "google_speech_v2_ws_connections",
			Help: "Current live websocket connections",
		}),
		WSUpgradeFailed: prometheus.NewCounter(prometheus.CounterOpts{
		Name: "google_speech_v2_ws_upgrade_failed_total",
			Help: "WS upgrade failures",
		}),
		AudioBytesIn: prometheus.NewCounter(prometheus.CounterOpts{
		Name: "google_speech_v2_audio_bytes_in_total",
			Help: "Audio bytes received from FreeSWITCH",
		}),
		AudioBytesToGoogle: prometheus.NewCounter(prometheus.CounterOpts{
		Name: "google_speech_v2_audio_bytes_to_google_total",
			Help: "Audio bytes sent to Google STT",
		}),
		AudioOverruns: prometheus.NewCounter(prometheus.CounterOpts{
		Name: "google_speech_v2_audio_overruns_total",
			Help: "Count of ring buffer overruns (drop-oldest)",
		}),
		GoogleResponses: prometheus.NewCounter(prometheus.CounterOpts{
		Name: "google_speech_v2_google_responses_total",
			Help: "Count of streaming responses received from Google",
		}),
		GoogleIdleTimeouts: prometheus.NewCounter(prometheus.CounterOpts{
		Name: "google_speech_v2_google_idle_timeouts_total",
			Help: "Count of idle timeouts that closed Google streams",
		}),
	}

	reg.MustRegister(
		m.WSConnections,
		m.WSUpgradeFailed,
		m.AudioBytesIn,
		m.AudioBytesToGoogle,
		m.AudioOverruns,
		m.GoogleResponses,
		m.GoogleIdleTimeouts,
	)
	return m
}

func (m *Metrics) Handler() http.Handler {
	return promhttp.HandlerFor(m.reg, promhttp.HandlerOpts{})
}