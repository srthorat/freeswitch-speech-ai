package main

import (
	"context"
	"log"
	"net/http"
	"os"
	"os/signal"
	"runtime"
	"syscall"
	"time"

	"google-speech-service-v2/internal/config"
	"google-speech-service-v2/internal/interfaces"
	"google-speech-service-v2/internal/logx"
	"google-speech-service-v2/internal/metrics"
	"google-speech-service-v2/internal/providers/aws"
	"google-speech-service-v2/internal/providers/google_v1"
	"google-speech-service-v2/internal/providers/google_v2"
	"google-speech-service-v2/internal/providers/mock"
	"google-speech-service-v2/internal/publish"
	"google-speech-service-v2/internal/wsserver"
)

var (
	// optionally set by -ldflags
	gitSha    = ""
	buildTime = ""
)

func main() {
	cfg, err := config.FromEnv()
	if err != nil {
		log.Fatalf("config: %v", err)
	}

	logger := logx.New(cfg.LogLevel, cfg.LogFormat)
	logger.Info("google-speech-service-v2 starting",
		"git_sha", gitSha,
		"build_time", buildTime,
		"ws_listen", cfg.WSListenAddr,
		"admin_listen", cfg.AdminListenAddr,
		"gomaxprocs", runtime.GOMAXPROCS(0),
		"target_sample_rate", cfg.TargetSampleRate,
		"max_buffer_ms", cfg.MaxBufferMs,
		"default_provider", cfg.DefaultProvider,
		"google_project", cfg.GCPProject,
		"default_location", cfg.GCPDefaultLocation,
	)

	m := metrics.New()

	// Use mock publisher for load testing, real Pusher for production
	var pub publish.Publisher
	if cfg.DefaultProvider == "mock" {
		pub, err = publish.NewMockPublisher(cfg, logger)
		if err != nil {
			logger.Fatal("mock publisher init failed", "err", err)
		}
	} else {
		pub, err = publish.NewPusherPublisher(cfg, logger)
		if err != nil {
			logger.Fatal("pusher init failed", "err", err)
		}
	}

	// Provider Factory
	// In a real high-scale app, we might want to cache clients or use a proper DI container.
	// For now, simple switch is effective.
	// Note: internal/providers/* usually have their own internal connection pooling (like gRPC client).
	
	// Pre-initialize Google V2 client to share gRPC connection
	var googleV2Client *google_v2.Client
	if cfg.GCPProject != "" {
		var gErr error
		googleV2Client, gErr = google_v2.NewClient(cfg, logger, m)
		if gErr != nil {
			logger.Warn("google-v2 init failed (continuing, but google-v2 will fail)", "err", gErr)
		} else {
			defer googleV2Client.Close()
		}
	}

	providerFactory := func(name string) interfaces.Provider {
		switch name {
		case "google-v2":
			if googleV2Client == nil {
				return nil
			}
			return googleV2Client
		case "aws":
			returnFunc, _ := aws.NewClient(cfg, logger, m)
			if returnFunc == nil {
				logger.Warn("aws provider failed to init (check credentials)")
				return nil
			} 
			return returnFunc
		case "google-v1":
			returnFunc, _ := google_v1.NewClient(cfg, logger, m)
			return returnFunc
		case "mock":
			// Mock provider for load testing - no real API calls
			returnFunc, _ := mock.NewClient(cfg, logger, m)
			return returnFunc
		default:
			return nil
		}
	}

	ws := wsserver.New(wsserver.Dependencies{
		Cfg:            cfg,
		ProviderFactory: providerFactory,
		Publisher:      pub,
		Metrics:        m,
		Logger:         logger,
	})

	adminMux := http.NewServeMux()
	adminMux.HandleFunc("/healthz", func(w http.ResponseWriter, r *http.Request) { w.WriteHeader(http.StatusOK) })
	adminMux.Handle("/metrics", m.Handler())

	wsMux := http.NewServeMux()
	wsMux.Handle("/audio-fork", ws)

	wsSrv := &http.Server{
		Addr:              cfg.WSListenAddr,
		Handler:           wsMux,
		ReadHeaderTimeout: 10 * time.Second,
	}
	adminSrv := &http.Server{
		Addr:              cfg.AdminListenAddr,
		Handler:           adminMux,
		ReadHeaderTimeout: 10 * time.Second,
	}

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()

	go func() {
		logger.Info("WS server listening", "addr", cfg.WSListenAddr)
		if err := wsSrv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			logger.Fatal("ws ListenAndServe failed", "err", err)
		}
	}()

	go func() {
		logger.Info("Admin server listening", "addr", cfg.AdminListenAddr)
		if err := adminSrv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			logger.Fatal("admin ListenAndServe failed", "err", err)
		}
	}()

	<-ctx.Done()
	logger.Info("shutdown requested")

	shutdownCtx, cancel := context.WithTimeout(context.Background(), 12*time.Second)
	defer cancel()

	_ = wsSrv.Shutdown(shutdownCtx)
	_ = adminSrv.Shutdown(shutdownCtx)

	logger.Info("shutdown complete")
}