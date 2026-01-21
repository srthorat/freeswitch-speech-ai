package config

import (
	"errors"
	"os"
	"strconv"
	"time"
)

type Config struct {
	WSListenAddr    string
	AdminListenAddr string

	// General
	DefaultProvider string // "google-v2", "google-v1", "aws"

	// Audio
	TargetSampleRate int // must be 16000
	MaxBufferMs      int // bounded buffering per session (drop-oldest)

	// Google STT v2
	GCPProject          string
	GCPDefaultLocation  string
	GCPRecognizerID     string
	GCPEnableAutoCreate bool
	GCPModel            string
	GCPInterimResults   bool
	GCPVADTimeoutMs     int

	GoogleStreamIdleTimeout time.Duration
	GoogleEndpointTemplate  string // e.g. "%s-speech.googleapis.com:443"

	// AWS
	AWSAccessKeyID     string
	AWSSecretAccessKey string
	AWSSessionToken    string
	AWSRegion          string

	// Pusher
	PusherAppID   string
	PusherKey     string
	PusherSecret  string
	PusherCluster string

	// Logging
	LogLevel  string // debug|info|warn|error
	LogFormat string // json|text
}

func FromEnv() (*Config, error) {
	cfg := &Config{
		WSListenAddr:          getEnv("SIDECAR_WS_LISTEN", getEnv("GOOGLE_SPEECH_V2_WS_LISTEN", "127.0.0.1:8088")),
		AdminListenAddr:       getEnv("SIDECAR_ADMIN_LISTEN", getEnv("GOOGLE_SPEECH_V2_ADMIN_LISTEN", "127.0.0.1:8089")),
		DefaultProvider:       getEnv("DEFAULT_PROVIDER", "google-v2"),
		TargetSampleRate:      mustInt(getEnv("SIDECAR_TARGET_SAMPLE_RATE", "16000")),
		MaxBufferMs:           mustInt(getEnv("SIDECAR_MAX_BUFFER_MS", "250")),
		GCPProject:            os.Getenv("GCP_PROJECT"),
		GCPDefaultLocation:    getEnv("GCP_LOCATION_DEFAULT", "us-central1"),
		GCPRecognizerID:       getEnv("GCP_RECOGNIZER_ID", "fs-stt"),
		GCPEnableAutoCreate:   getEnvBool("GCP_RECOGNIZER_AUTOCREATE", true),
		GCPModel:              os.Getenv("GCP_MODEL"),
		GCPInterimResults:     getEnvBool("GCP_INTERIM_RESULTS", true),
		GCPVADTimeoutMs:       mustInt(getEnv("GCP_VAD_TIMEOUT_MS", "1500")),
		GoogleStreamIdleTimeout: time.Second * time.Duration(mustInt(getEnv("GCP_STREAM_IDLE_SECS", "20"))),
		GoogleEndpointTemplate:  getEnv("GCP_ENDPOINT_TEMPLATE", "%s-speech.googleapis.com:443"),
		
		AWSAccessKeyID:     os.Getenv("AWS_ACCESS_KEY_ID"),
		AWSSecretAccessKey: os.Getenv("AWS_SECRET_ACCESS_KEY"),
		AWSSessionToken:    os.Getenv("AWS_SESSION_TOKEN"),
		AWSRegion:          getEnv("AWS_REGION", "us-east-1"),

		PusherAppID:             os.Getenv("PUSHER_APP_ID"),
		PusherKey:               os.Getenv("PUSHER_KEY"),
		PusherSecret:            os.Getenv("PUSHER_SECRET"),
		PusherCluster:           os.Getenv("PUSHER_CLUSTER"),
		LogLevel:                getEnv("LOG_LEVEL", "info"),
		LogFormat:               getEnv("LOG_FORMAT", "json"),
	}

	if cfg.TargetSampleRate != 16000 {
		return nil, errors.New("GOOGLE_SPEECH_V2_TARGET_SAMPLE_RATE must be 16000")
	}
	if cfg.GCPProject == "" && (cfg.DefaultProvider == "google-v2" || cfg.DefaultProvider == "google-v1") {
		return nil, errors.New("GCP_PROJECT is required for google providers")
	}
	// Pusher credentials only required for non-mock providers
	if cfg.DefaultProvider != "mock" {
		if cfg.PusherAppID == "" || cfg.PusherKey == "" || cfg.PusherSecret == "" || cfg.PusherCluster == "" {
			return nil, errors.New("PUSHER_APP_ID, PUSHER_KEY, PUSHER_SECRET, PUSHER_CLUSTER are required")
		}
	}
	return cfg, nil
}

func getEnv(k, def string) string {
	if v := os.Getenv(k); v != "" {
		return v
	}
	return def
}

func getEnvBool(k string, def bool) bool {
	v := os.Getenv(k)
	if v == "" {
		return def
	}
	b, err := strconv.ParseBool(v)
	if err != nil {
		return def
	}
	return b
}

func mustInt(s string) int {
	i, _ := strconv.Atoi(s)
	return i
}