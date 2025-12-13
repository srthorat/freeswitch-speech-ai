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
	"google-speech-service-v2/internal/google"
	"google-speech-service-v2/internal/logx"
	"google-speech-service-v2/internal/metrics"
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
		"google_project", cfg.GCPProject,
		"default_location", cfg.GCPDefaultLocation,
	)

	m := metrics.New()

	pub, err := publish.NewPusherPublisher(cfg, logger)
	if err != nil {
		logger.Fatal("pusher init failed", "err", err)
	}

	g, err := google.NewClient(cfg, logger, m)
	if err != nil {
		logger.Fatal("google init failed", "err", err)
	}
	defer g.Close()

	ws := wsserver.New(wsserver.Dependencies{
		Cfg:       cfg,
		Google:    g,
		Publisher: pub,
		Metrics:   m,
		Logger:    logger,
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
