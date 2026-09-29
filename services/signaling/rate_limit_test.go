package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/gorilla/websocket"
)

func TestSourceLimiterEnforcesWindowAndRecovers(t *testing.T) {
	limiter := newSourceLimiter(defaultAPIWritesPerMinute)
	now := time.Unix(1000, 0)

	for attempt := 1; attempt <= defaultAPIWritesPerMinute; attempt++ {
		allowed, retryAfter := limiter.Allow("203.0.113.10", now)
		if !allowed || retryAfter != 0 {
			t.Fatalf("attempt %d: allowed=%v retry=%s", attempt, allowed, retryAfter)
		}
	}

	allowed, retryAfter := limiter.Allow("203.0.113.10", now)
	if allowed {
		t.Fatal("31st mutation in one minute must be rejected")
	}
	if retryAfter <= 0 || retryAfter > sourceLimitWindow {
		t.Fatalf("retry after = %s", retryAfter)
	}

	allowed, retryAfter = limiter.Allow(
		"203.0.113.10", now.Add(sourceLimitWindow))
	if !allowed || retryAfter != 0 {
		t.Fatalf("new window: allowed=%v retry=%s", allowed, retryAfter)
	}
}

func TestSourceLimiterBoundsAndPrunesStorage(t *testing.T) {
	limiter := newSourceLimiter(1)
	now := time.Unix(2000, 0)

	for index := 0; index < maxSourceLimiterEntries; index++ {
		source := fmt.Sprintf("source-%d", index)
		allowed, _ := limiter.Allow(source, now)
		if !allowed {
			t.Fatalf("source %d rejected before storage bound", index)
		}
	}
	if len(limiter.entries) != maxSourceLimiterEntries {
		t.Fatalf("entry count = %d", len(limiter.entries))
	}

	allowed, retryAfter := limiter.Allow("overflow", now)
	if allowed || retryAfter <= 0 {
		t.Fatalf("overflow: allowed=%v retry=%s", allowed, retryAfter)
	}
	if len(limiter.entries) != maxSourceLimiterEntries {
		t.Fatalf("overflow grew map to %d", len(limiter.entries))
	}

	allowed, retryAfter = limiter.Allow(
		"replacement", now.Add(sourceLimitWindow+time.Second))
	if !allowed || retryAfter != 0 {
		t.Fatalf("after prune: allowed=%v retry=%s", allowed, retryAfter)
	}
	if len(limiter.entries) > maxSourceLimiterEntries {
		t.Fatalf("pruned map exceeds bound: %d", len(limiter.entries))
	}
}

func TestRequestSourceIgnoresForgedForwardedHeaderByDefault(t *testing.T) {
	request := httptest.NewRequest("POST", "https://catro.example.test/v1/users/register", nil)
	request.RemoteAddr = "198.51.100.20:50123"
	request.Header.Set("X-Forwarded-For", "203.0.113.99")

	if got := requestSource(request, false); got != "198.51.100.20" {
		t.Fatalf("source = %q", got)
	}
	if got := requestSource(request, true); got != "198.51.100.20" {
		t.Fatalf("public direct caller forged proxy source = %q", got)
	}
}

func TestRequestSourceTrustsFirstValidForwardedIPFromPrivateProxy(t *testing.T) {
	request := httptest.NewRequest("POST", "https://catro.example.test/v1/users/register", nil)
	request.RemoteAddr = "172.20.0.5:41000"
	request.Header.Set("X-Forwarded-For", "not-an-ip, 203.0.113.7, 198.51.100.8")

	if got := requestSource(request, true); got != "203.0.113.7" {
		t.Fatalf("source = %q", got)
	}
}

func TestAPIRateLimitRejectsThirtyFirstMutation(t *testing.T) {
	d, err := openDirectory(
		filepath.Join(t.TempDir(), "directory.json"),
		[]byte(strings.Repeat("r", 32)))
	if err != nil {
		t.Fatal(err)
	}
	s := &service{
		apiLimiter: newSourceLimiter(defaultAPIWritesPerMinute),
		rooms:      make(map[string]*room),
	}
	handler := s.limitDirectoryMutation(d.handleRegister)
	body, err := json.Marshal(map[string]any{
		"user_id":      "limited-user",
		"display_name": "Limited User",
		"credential":   testCredential(0x77),
	})
	if err != nil {
		t.Fatal(err)
	}

	for attempt := 1; attempt <= defaultAPIWritesPerMinute+1; attempt++ {
		request := httptest.NewRequest(
			http.MethodPost,
			"/v1/users/register",
			bytes.NewReader(body))
		request.RemoteAddr = "198.51.100.30:41234"
		response := httptest.NewRecorder()
		handler(response, request)

		if attempt <= defaultAPIWritesPerMinute {
			if response.Code != http.StatusCreated && response.Code != http.StatusOK {
				t.Fatalf("attempt %d: status %d body %s",
					attempt, response.Code, response.Body.String())
			}
			continue
		}
		if response.Code != http.StatusTooManyRequests {
			t.Fatalf("31st mutation status = %d body %s",
				response.Code, response.Body.String())
		}
		if response.Header().Get("Retry-After") == "" {
			t.Fatal("429 response is missing Retry-After")
		}
	}

	if s.apiRateLimitRejections.Load() != 1 {
		t.Fatalf("rate-limit rejection metric = %d",
			s.apiRateLimitRejections.Load())
	}
	metrics := httptest.NewRecorder()
	s.metrics(metrics, httptest.NewRequest(http.MethodGet, "/metrics", nil))
	if !strings.Contains(
		metrics.Body.String(),
		"catro_signaling_api_rate_limit_rejections_total 1\n") {
		t.Fatalf("metrics did not expose rate-limit rejection:\n%s",
			metrics.Body.String())
	}
}

func TestProductionMetricsTrackRoomEventsWithoutSensitiveLabels(t *testing.T) {
	s, server, secret := newTestRoomService(t)
	owner, _ := joinTestRoomPeer(t, server, secret, "peer-1")
	guest, _ := joinTestRoomPeer(t, server, secret, "peer-2")
	peer3, _ := joinTestRoomPeer(t, server, secret, "peer-3")
	peer4, _ := joinTestRoomPeer(t, server, secret, "peer-4")
	peer5, _ := joinTestRoomPeer(t, server, secret, "peer-5")
	peers := []*websocket.Conn{owner, guest, peer3, peer4, peer5}

	claimTestScreen(t, owner)
	for _, peer := range peers {
		_ = readTestRoomMessage(t, peer, "screen_state")
	}
	claimTestScreen(t, guest)
	_ = readTestRoomMessage(t, guest, "screen_busy")
	activeMetrics := httptest.NewRecorder()
	s.metrics(activeMetrics, httptest.NewRequest(http.MethodGet, "/metrics", nil))
	if !strings.Contains(
		activeMetrics.Body.String(),
		"catro_signaling_active_screen_publishers 1\n") {
		t.Fatalf("active screen publisher gauge not set:\n%s",
			activeMetrics.Body.String())
	}
	if err := owner.WriteJSON(map[string]any{"type": "screen_release"}); err != nil {
		t.Fatal(err)
	}
	for _, peer := range peers {
		_ = readTestRoomMessage(t, peer, "screen_state")
	}

	token, err := mintToken(
		[]byte(secret),
		claims{
			ServerID:  "server-1",
			ChannelID: "voice-1",
			PeerID:    "peer-6",
			Expires:   time.Now().Add(time.Minute).Unix(),
		})
	if err != nil {
		t.Fatal(err)
	}
	socketURL := "ws" + strings.TrimPrefix(server.URL, "http") + "/v1/rtc"
	sixth, _, err := websocket.DefaultDialer.Dial(socketURL, nil)
	if err != nil {
		t.Fatal(err)
	}
	defer sixth.Close()
	if err := sixth.WriteJSON(map[string]any{
		"type":       "join",
		"protocol":   1,
		"token":      token,
		"server_id":  "server-1",
		"channel_id": "voice-1",
		"peer_id":    "peer-6",
	}); err != nil {
		t.Fatal(err)
	}
	var rejected testRoomMessage
	if err := sixth.ReadJSON(&rejected); err != nil {
		t.Fatal(err)
	}
	if rejected.Type != "error" || rejected.Message != "room capacity reached" {
		t.Fatalf("capacity response = %#v", rejected)
	}

	response := httptest.NewRecorder()
	s.metrics(response, httptest.NewRequest(http.MethodGet, "/metrics", nil))
	body := response.Body.String()
	for _, want := range []string{
		"catro_signaling_active_rooms 1\n",
		"catro_signaling_active_screen_publishers 0\n",
		"catro_signaling_room_capacity_rejections_total 1\n",
		"catro_signaling_screen_claims_total 1\n",
		"catro_signaling_screen_releases_total 1\n",
		"catro_signaling_screen_busy_rejections_total 1\n",
	} {
		if !strings.Contains(body, want) {
			t.Fatalf("metrics missing %q:\n%s", want, body)
		}
	}
	for _, secretValue := range []string{
		"peer-1", "peer-6", "server-1", "voice-1", "198.51.100.30",
	} {
		if strings.Contains(body, secretValue) {
			t.Fatalf("metrics leaked %q:\n%s", secretValue, body)
		}
	}
}


func TestRunHealthCheckAcceptsOnlyHealthyHTTP(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(
		func(w http.ResponseWriter, r *http.Request) {
			if r.URL.Path == "/healthz" {
				w.WriteHeader(http.StatusOK)
				return
			}
			http.Error(w, "unhealthy", http.StatusServiceUnavailable)
		}))
	defer server.Close()

	if err := runHealthCheck(server.URL + "/healthz"); err != nil {
		t.Fatalf("healthy endpoint rejected: %v", err)
	}
	if err := runHealthCheck(server.URL + "/unhealthy"); err == nil {
		t.Fatal("unhealthy endpoint accepted")
	}
	if err := runHealthCheck("file:///etc/passwd"); err == nil {
		t.Fatal("non-HTTP health endpoint accepted")
	}
}


func TestDiscoveryRateLimitRejectsThirtyFirstLookup(t *testing.T) {
	s := &service{
		discoveryLimiter: newSourceLimiter(defaultDiscoveryReadsPerMinute),
		rooms:            make(map[string]*room),
	}
	handler := s.limitDirectoryLookup(func(w http.ResponseWriter, _ *http.Request) {
		w.WriteHeader(http.StatusOK)
	})

	for attempt := 1; attempt <= defaultDiscoveryReadsPerMinute+1; attempt++ {
		request := httptest.NewRequest(
			http.MethodGet,
			"/v1/server-lookup?code=CAT-0000-0000-0000-0000-0000",
			nil)
		request.RemoteAddr = "198.51.100.40:50000"
		response := httptest.NewRecorder()
		handler(response, request)

		if attempt <= defaultDiscoveryReadsPerMinute {
			if response.Code != http.StatusOK {
				t.Fatalf("lookup %d status = %d", attempt, response.Code)
			}
			continue
		}
		if response.Code != http.StatusTooManyRequests {
			t.Fatalf("31st lookup status = %d", response.Code)
		}
		if response.Header().Get("Retry-After") == "" {
			t.Fatal("discovery 429 missing Retry-After")
		}
	}
	if s.discoveryRateLimitRejections.Load() != 1 {
		t.Fatalf("discovery rate metric = %d",
			s.discoveryRateLimitRejections.Load())
	}
}
