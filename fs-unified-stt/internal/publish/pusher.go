package publish

import (
	"errors"
	"regexp"

	"github.com/pusher/pusher-http-go/v5"

	"google-speech-service-v2/internal/config"
	"google-speech-service-v2/internal/logx"
)

type PusherPublisher struct {
	client *pusher.Client
	log    *logx.Logger
}

var likelyBadChannelChars = regexp.MustCompile(`[^\w\-\=\@\.\:]`)

func NewPusherPublisher(cfg *config.Config, logger *logx.Logger) (*PusherPublisher, error) {
	if cfg.PusherAppID == "" || cfg.PusherKey == "" || cfg.PusherSecret == "" || cfg.PusherCluster == "" {
		return nil, errors.New("missing pusher credentials")
	}
	return &PusherPublisher{
		log: logger,
		client: &pusher.Client{
			AppID:   cfg.PusherAppID,
			Key:     cfg.PusherKey,
			Secret:  cfg.PusherSecret,
			Cluster: cfg.PusherCluster,
			Secure:  true,
		},
	}, nil
}

func (p *PusherPublisher) Publish(channel, event string, payload any) error {
	// You required: channel name = sip_call_id as-is.
	// Warn if it likely contains disallowed chars.
	if likelyBadChannelChars.MatchString(channel) {
		p.log.Warn("pusher channel may contain invalid characters",
			"channel", channel,
		)
	}
	return p.client.Trigger(channel, event, payload)
}