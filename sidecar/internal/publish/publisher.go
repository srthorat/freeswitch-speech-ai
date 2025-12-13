package publish

type Publisher interface {
	Publish(channel, event string, payload any) error
}

type Speaker struct {
	Name  string `json:"name"`
	Phone string `json:"phone"`
}

type TranscriptEvent struct {
	UUID      string `json:"uuid"`
	SIPCallID string `json:"sip_call_id"`

	Channel int    `json:"channel"`
	Role    string `json:"role"` // caller/callee

	Caller Speaker `json:"caller"`
	Callee Speaker `json:"callee"`

	Text       string  `json:"text"`
	Confidence float32 `json:"confidence"`
	IsFinal    bool    `json:"is_final"`
	TsMS       int64   `json:"ts_ms"`
}