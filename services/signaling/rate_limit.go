package main

import (
	"fmt"
	"net"
	"net/http"
	"os"
	"strconv"
	"strings"
	"sync"
	"time"
)

const (
	sourceLimitWindow         = time.Minute
	maxSourceLimiterEntries   = 4096
	defaultAPIWritesPerMinute      = 30
	maximumAPIWritesPerMinute      = 300
	defaultDiscoveryReadsPerMinute = 30
	maximumDiscoveryReadsPerMinute = 300
)

type sourceLimitEntry struct {
	started time.Time
	count   int
}

type sourceLimiter struct {
	mu      sync.Mutex
	limit   int
	entries map[string]sourceLimitEntry
}

func newSourceLimiter(limit int) *sourceLimiter {
	return &sourceLimiter{
		limit:   limit,
		entries: make(map[string]sourceLimitEntry),
	}
}

func (l *sourceLimiter) Allow(source string, now time.Time) (bool, time.Duration) {
	if l == nil || l.limit <= 0 {
		return false, sourceLimitWindow
	}
	if source == "" {
		source = "unknown"
	}

	l.mu.Lock()
	defer l.mu.Unlock()

	l.pruneLocked(now)
	if entry, exists := l.entries[source]; exists {
		if now.Before(entry.started) || !now.Before(entry.started.Add(sourceLimitWindow)) {
			l.entries[source] = sourceLimitEntry{started: now, count: 1}
			return true, 0
		}
		if entry.count >= l.limit {
			return false, entry.started.Add(sourceLimitWindow).Sub(now)
		}
		entry.count++
		l.entries[source] = entry
		return true, 0
	}

	if len(l.entries) >= maxSourceLimiterEntries {
		return false, l.nextExpiryLocked(now)
	}
	l.entries[source] = sourceLimitEntry{started: now, count: 1}
	return true, 0
}

func (l *sourceLimiter) pruneLocked(now time.Time) {
	for source, entry := range l.entries {
		if now.Before(entry.started) || now.Before(entry.started.Add(sourceLimitWindow)) {
			continue
		}
		delete(l.entries, source)
	}
}

func (l *sourceLimiter) nextExpiryLocked(now time.Time) time.Duration {
	retryAfter := sourceLimitWindow
	for _, entry := range l.entries {
		remaining := entry.started.Add(sourceLimitWindow).Sub(now)
		if remaining > 0 && remaining < retryAfter {
			retryAfter = remaining
		}
	}
	return retryAfter
}

func environmentInt(name string, fallback int) (int, error) {
	raw := strings.TrimSpace(os.Getenv(name))
	if raw == "" {
		return fallback, nil
	}
	value, err := strconv.Atoi(raw)
	if err != nil {
		return 0, fmt.Errorf("%s must be an integer", name)
	}
	return value, nil
}

func environmentBool(name string, fallback bool) (bool, error) {
	raw := strings.TrimSpace(os.Getenv(name))
	if raw == "" {
		return fallback, nil
	}
	value, err := strconv.ParseBool(raw)
	if err != nil {
		return false, fmt.Errorf("%s must be a boolean", name)
	}
	return value, nil
}

func requestSource(r *http.Request, trustProxyHeaders bool) string {
	peer := parseRemoteIP(r.RemoteAddr)
	if trustProxyHeaders && isTrustedProxyPeer(peer) {
		for _, value := range strings.Split(r.Header.Get("X-Forwarded-For"), ",") {
			candidate := net.ParseIP(strings.TrimSpace(value))
			if candidate != nil {
				return candidate.String()
			}
		}
	}
	if peer != nil {
		return peer.String()
	}
	if fallback := strings.TrimSpace(r.RemoteAddr); fallback != "" {
		return fallback
	}
	return "unknown"
}

func parseRemoteIP(remoteAddr string) net.IP {
	host, _, err := net.SplitHostPort(strings.TrimSpace(remoteAddr))
	if err == nil {
		return net.ParseIP(host)
	}
	return net.ParseIP(strings.TrimSpace(remoteAddr))
}

func isTrustedProxyPeer(ip net.IP) bool {
	return ip != nil && (ip.IsPrivate() || ip.IsLoopback() || ip.IsLinkLocalUnicast())
}

func (s *service) limitDirectoryMutation(next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if r.Method == http.MethodPost && s.apiLimiter != nil {
			allowed, retryAfter := s.apiLimiter.Allow(
				requestSource(r, s.trustProxyHeaders), time.Now())
			if !allowed {
				s.apiRateLimitRejections.Add(1)
				seconds := int64((retryAfter + time.Second - 1) / time.Second)
				if seconds < 1 {
					seconds = 1
				}
				w.Header().Set("Retry-After", strconv.FormatInt(seconds, 10))
				writeAPIError(w, http.StatusTooManyRequests, "rate limit exceeded")
				return
			}
		}
		next(w, r)
	}
}


func (s *service) limitDirectoryLookup(next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if r.Method == http.MethodGet && s.discoveryLimiter != nil {
			allowed, retryAfter := s.discoveryLimiter.Allow(
				requestSource(r, s.trustProxyHeaders), time.Now())
			if !allowed {
				s.discoveryRateLimitRejections.Add(1)
				seconds := int64((retryAfter + time.Second - 1) / time.Second)
				if seconds < 1 {
					seconds = 1
				}
				w.Header().Set("Retry-After", strconv.FormatInt(seconds, 10))
				writeAPIError(w, http.StatusTooManyRequests, "rate limit exceeded")
				return
			}
		}
		next(w, r)
	}
}
