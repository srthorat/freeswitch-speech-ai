package logx

import (
	"encoding/json"
	"fmt"
	"log"
	"os"
	"strings"
	"time"
)

type Logger struct {
	level  int
	format string // json|text
}

const (
	lvlDebug = iota
	lvlInfo
	lvlWarn
	lvlError
	lvlFatal
)

func New(level, format string) *Logger {
	return &Logger{
		level:  parseLevel(level),
		format: strings.ToLower(format),
	}
}

func parseLevel(s string) int {
	switch strings.ToLower(s) {
	case "debug":
		return lvlDebug
	case "info":
		return lvlInfo
	case "warn", "warning":
		return lvlWarn
	case "error":
		return lvlError
	case "fatal":
		return lvlFatal
	default:
		return lvlInfo
	}
}

func (l *Logger) Debug(msg string, kv ...any) { l.log(lvlDebug, "DEBUG", msg, kv...) }
func (l *Logger) Info(msg string, kv ...any)  { l.log(lvlInfo, "INFO", msg, kv...) }
func (l *Logger) Warn(msg string, kv ...any)  { l.log(lvlWarn, "WARN", msg, kv...) }
func (l *Logger) Error(msg string, kv ...any) { l.log(lvlError, "ERROR", msg, kv...) }
func (l *Logger) Fatal(msg string, kv ...any) { l.log(lvlFatal, "FATAL", msg, kv...) }

func (l *Logger) log(which int, lvl, msg string, kv ...any) {
	if which < l.level {
		return
	}

	ts := time.Now().UTC().Format(time.RFC3339Nano)

	if l.format == "json" {
		m := map[string]any{
			"ts":   ts,
			"lvl":  lvl,
			"msg":  msg,
			"pid":  os.Getpid(),
		}
		for i := 0; i+1 < len(kv); i += 2 {
			k, ok := kv[i].(string)
			if !ok {
				continue
			}
			m[k] = kv[i+1]
		}
		b, _ := json.Marshal(m)
		_, _ = os.Stdout.Write(append(b, '\n'))
	} else {
		// text
		fmtKv := ""
		for i := 0; i+1 < len(kv); i += 2 {
			fmtKv += fmt.Sprintf(" %v=%v", kv[i], kv[i+1])
		}
		log.Printf("%s %s %s%s", ts, lvl, msg, fmtKv)
	}

	if which == lvlFatal {
		os.Exit(1)
	}
}