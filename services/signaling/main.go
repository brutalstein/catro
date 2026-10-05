package main

import (
	"context"
	"crypto/hmac"
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"log"
	"net/http"
	"os"
	"os/signal"
	"strings"
	"sync"
	"sync/atomic"
	"syscall"
	"time"

	"github.com/gorilla/websocket"
)

const (
	maxSignalBytes        = 64 * 1024
	maxCandidateBytes     = 4096
	maxIDBytes            = 128
	defaultMaxRoom        = 5
	maximumProductionRoom = 5
	writeTimeout          = 5 * time.Second
	readTimeout           = 45 * time.Second
	pingInterval          = 20 * time.Second
	joinTimeout           = 8 * time.Second
	signalWindow          = 10 * time.Second
	maxSignalsPerWindow   = 256
)

var errRoomCapacity = errors.New("room capacity reached")

type claims struct {
	ServerID  string `json:"server_id"`
	ChannelID string `json:"channel_id"`
	PeerID    string `json:"peer_id"`
	Expires   int64  `json:"exp"`
}

type message struct {
	Type        string   `json:"type"`
	Token       string   `json:"token,omitempty"`
	ServerID    string   `json:"server_id,omitempty"`
	ChannelID   string   `json:"channel_id,omitempty"`
	PeerID      string   `json:"peer_id,omitempty"`
	Protocol    int      `json:"protocol,omitempty"`
	Peers       []string `json:"peers,omitempty"`
	To          string   `json:"to,omitempty"`
	From        string   `json:"from,omitempty"`
	Kind        string   `json:"kind,omitempty"`
	SDP         string   `json:"sdp,omitempty"`
	Candidate   string   `json:"candidate,omitempty"`
	MID         string   `json:"mid,omitempty"`
	ScreenOwner string   `json:"screen_owner,omitempty"`
	Message     string   `json:"message,omitempty"`
}

type client struct {
	id      string
	room    *room
	conn    *websocket.Conn
	writeMu sync.Mutex

	rateMu      sync.Mutex
	rateStarted time.Time
	rateCount   int
}

type room struct {
	key         string
	mu          sync.RWMutex
	peers       map[string]*client
	maxPeers    int
	screenOwner string
}

type service struct {
	secret            []byte
	maxRoomPeers      int
	allowedOrigin     string
	apiLimiter        *sourceLimiter
	discoveryLimiter  *sourceLimiter
	trustProxyHeaders bool

	roomsMu sync.Mutex
	rooms   map[string]*room

	activeConnections      atomic.Int64
	signalMessages         atomic.Uint64
	rejectedMessages       atomic.Uint64
	roomCapacityRejections atomic.Uint64
	screenClaims           atomic.Uint64
	screenReleases         atomic.Uint64
	screenBusyRejections   atomic.Uint64
	apiRateLimitRejections       atomic.Uint64
	discoveryRateLimitRejections atomic.Uint64
}

func main() {
	apiWritesDefault, err := environmentInt(
		"CATRO_API_WRITES_PER_MINUTE", defaultAPIWritesPerMinute)
	if err != nil {
		log.Fatal(err)
	}
	discoveryReadsDefault, err := environmentInt(
		"CATRO_DISCOVERY_READS_PER_MINUTE", defaultDiscoveryReadsPerMinute)
	if err != nil {
		log.Fatal(err)
	}
	trustProxyDefault, err := environmentBool(
		"CATRO_TRUST_PROXY_HEADERS", false)
	if err != nil {
		log.Fatal(err)
	}

	addr := flag.String("addr", ":8443", "listen address")
	secret := flag.String("secret", os.Getenv("CATRO_SIGNALING_SECRET"), "HMAC secret")
	stateFileDefault := os.Getenv("CATRO_SIGNALING_STATE")
	if stateFileDefault == "" {
		stateFileDefault = "catro-directory.json"
	}
	stateFile := flag.String("state-file", stateFileDefault, "persistent membership state file")
	tlsCert := flag.String("tls-cert", os.Getenv("CATRO_SIGNALING_TLS_CERT"), "TLS certificate path")
	tlsKey := flag.String("tls-key", os.Getenv("CATRO_SIGNALING_TLS_KEY"), "TLS private key path")
	maxRoom := flag.Int("max-room-peers", defaultMaxRoom, "maximum peers per voice room")
	apiWritesPerMinute := flag.Int(
		"api-writes-per-minute",
		apiWritesDefault,
		"per-source directory mutations allowed per minute")
	discoveryReadsPerMinute := flag.Int(
		"discovery-reads-per-minute",
		discoveryReadsDefault,
		"per-source exact Server Code lookups allowed per minute")
	trustProxyHeaders := flag.Bool(
		"trust-proxy-headers",
		trustProxyDefault,
		"trust X-Forwarded-For only from private/loopback proxy peers")
	origin := flag.String("allowed-origin", "", "optional exact browser Origin")
	allowHTTP := flag.Bool("allow-insecure-http", false, "engineering only: plaintext HTTP/WebSocket")
	allowNoTURN := flag.Bool("allow-no-turn", false, "engineering only: permit RTC provisioning without TURN")
	healthCheckURL := flag.String(
		"health-check",
		"",
		"probe one HTTP(S) health endpoint and exit")
	publicSignalingURL := flag.String(
		"public-signaling-url",
		os.Getenv("CATRO_PUBLIC_SIGNALING_URL"),
		"public wss:// signaling endpoint returned to authenticated clients")
	iceServerList := flag.String(
		"ice-servers",
		os.Getenv("CATRO_ICE_SERVERS"),
		"semicolon/comma separated STUN/TURN URLs returned to authenticated clients")
	turnSecret := flag.String(
		"turn-secret",
		os.Getenv("CATRO_TURN_SECRET"),
		"coturn REST shared secret used only to mint short-lived TURN credentials")
	mint := flag.Bool("mint-token", false, "mint one room token and exit")
	serverID := flag.String("server-id", "", "token server id")
	channelID := flag.String("channel-id", "", "token voice channel id")
	peerID := flag.String("peer-id", "", "token peer/user id")
	ttl := flag.Duration("ttl", 24*time.Hour, "minted token lifetime")
	flag.Parse()

	if *healthCheckURL != "" {
		if err := runHealthCheck(*healthCheckURL); err != nil {
			log.Fatal(err)
		}
		return
	}

	if len(*secret) < 32 {
		log.Fatal("CATRO signaling secret must be at least 32 bytes")
	}
	if *maxRoom < 2 || *maxRoom > maximumProductionRoom {
		log.Fatal("max-room-peers must be between 2 and 5")
	}
	if *apiWritesPerMinute < 1 ||
		*apiWritesPerMinute > maximumAPIWritesPerMinute {
		log.Fatal("api-writes-per-minute must be between 1 and 300")
	}
	if *discoveryReadsPerMinute < 1 ||
		*discoveryReadsPerMinute > maximumDiscoveryReadsPerMinute {
		log.Fatal("discovery-reads-per-minute must be between 1 and 300")
	}

	if *mint {
		if !validID(*serverID) || !validID(*channelID) || !validID(*peerID) || *ttl <= 0 {
			log.Fatal("--server-id, --channel-id, --peer-id and positive --ttl are required")
		}
		token, err := mintToken([]byte(*secret), claims{
			ServerID: *serverID, ChannelID: *channelID, PeerID: *peerID,
			Expires: time.Now().Add(*ttl).Unix(),
		})
		if err != nil {
			log.Fatal(err)
		}
		fmt.Println(token)
		return
	}

	if !*allowHTTP && (*tlsCert == "" || *tlsKey == "") {
		log.Fatal("TLS certificate/key required unless --allow-insecure-http is explicitly set")
	}

	iceServers := strings.FieldsFunc(
		*iceServerList,
		func(r rune) bool {
			return r == ';' || r == ','
		})
	for index := range iceServers {
		iceServers[index] = strings.TrimSpace(iceServers[index])
	}
	filteredICE := iceServers[:0]
	hasTURN := false
	for _, value := range iceServers {
		if value == "" {
			continue
		}
		if strings.HasPrefix(value, "turn:") ||
			strings.HasPrefix(value, "turns:") {
			hasTURN = true
		}
		filteredICE = append(filteredICE, value)
	}
	iceServers = filteredICE

	if *publicSignalingURL == "" || len(iceServers) == 0 {
		log.Fatal("--public-signaling-url and --ice-servers are required")
	}
	if *allowHTTP {
		if !strings.HasPrefix(*publicSignalingURL, "ws://") &&
			!strings.HasPrefix(*publicSignalingURL, "wss://") {
			log.Fatal("public signaling URL must use ws:// or wss://")
		}
	} else if !strings.HasPrefix(*publicSignalingURL, "wss://") {
		log.Fatal("production public signaling URL must use wss://")
	}
	if !*allowNoTURN && !hasTURN {
		log.Fatal("production ICE provisioning requires at least one TURN URL")
	}
	if hasTURN {
		if len(*turnSecret) < 32 {
			log.Fatal("CATRO TURN REST secret must be at least 32 bytes whenever TURN URLs are configured")
		}
		for _, value := range iceServers {
			lower := strings.ToLower(value)
			if (strings.HasPrefix(lower, "turn:") ||
				strings.HasPrefix(lower, "turns:")) &&
				strings.Contains(value, "@") {
				log.Fatal("base TURN URLs must not contain long-lived credentials; configure --turn-secret instead")
			}
		}
	}

	directory, err := openDirectory(*stateFile, []byte(*secret))
	if err != nil {
		log.Fatalf("directory: %v", err)
	}
	directory.setRTCProvisioning(
		*publicSignalingURL,
		iceServers,
		*allowHTTP,
		*allowNoTURN,
		[]byte(*turnSecret),
		*maxRoom)

	s := &service{
		secret:            []byte(*secret),
		maxRoomPeers:      *maxRoom,
		allowedOrigin:     *origin,
		apiLimiter:        newSourceLimiter(*apiWritesPerMinute),
		discoveryLimiter:  newSourceLimiter(*discoveryReadsPerMinute),
		trustProxyHeaders: *trustProxyHeaders,
		rooms:             make(map[string]*room),
	}
	directory.signaling = s

	mux := http.NewServeMux()
	mux.HandleFunc("/healthz", s.health)
	mux.HandleFunc("/metrics", s.metrics)
	mux.HandleFunc(
		"/v1/users/register",
		s.limitDirectoryMutation(directory.handleRegister))
	mux.HandleFunc(
		"/v1/servers/sync",
		s.limitDirectoryMutation(directory.handleServerSync))
	mux.HandleFunc("/v1/servers", directory.handleServers)
	mux.HandleFunc("/v1/members", directory.handleMembers)
	mux.HandleFunc(
		"/v1/server-lookup",
		s.limitDirectoryLookup(directory.handleServerLookup))
	mux.HandleFunc(
		"/v1/join-requests",
		s.limitDirectoryMutation(directory.handleJoinRequests))
	mux.HandleFunc(
		"/v1/join-requests/decision",
		s.limitDirectoryMutation(directory.handleJoinRequestDecision))
	mux.HandleFunc(
		"/v1/join-requests/cancel",
		s.limitDirectoryMutation(directory.handleJoinRequestCancel))
	mux.HandleFunc(
		"/v1/invites",
		s.limitDirectoryMutation(directory.handleInvites))
	mux.HandleFunc(
		"/v1/invites/accept",
		s.limitDirectoryMutation(directory.handleInviteAccept))
	mux.HandleFunc(
		"/v1/messages",
		s.limitDirectoryMutation(directory.handleMessages))
	mux.HandleFunc(
		"/v1/rtc-token",
		s.limitDirectoryMutation(directory.handleRTCToken))
	mux.HandleFunc("/v1/rtc", s.websocket)

	httpServer := &http.Server{
		Addr: *addr, Handler: mux,
		ReadHeaderTimeout: 5 * time.Second,
		IdleTimeout:       60 * time.Second,
		MaxHeaderBytes:    16 * 1024,
	}

	errs := make(chan error, 1)
	go func() {
		if *allowHTTP {
			errs <- httpServer.ListenAndServe()
		} else {
			errs <- httpServer.ListenAndServeTLS(*tlsCert, *tlsKey)
		}
	}()

	stop := make(chan os.Signal, 1)
	signal.Notify(stop, syscall.SIGINT, syscall.SIGTERM)
	select {
	case sig := <-stop:
		log.Printf("shutdown signal: %s", sig)
	case err := <-errs:
		if !errors.Is(err, http.ErrServerClosed) {
			log.Printf("server stopped: %v", err)
		}
	}

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	if err := httpServer.Shutdown(ctx); err != nil {
		log.Printf("shutdown: %v", err)
	}
}

func runHealthCheck(endpoint string) error {
	request, err := http.NewRequest(http.MethodGet, endpoint, nil)
	if err != nil || request.URL.Host == "" ||
		(request.URL.Scheme != "http" && request.URL.Scheme != "https") {
		return errors.New("health-check endpoint must be an absolute HTTP(S) URL")
	}

	client := &http.Client{
		Timeout: 2 * time.Second,
		CheckRedirect: func(*http.Request, []*http.Request) error {
			return http.ErrUseLastResponse
		},
	}
	response, err := client.Do(request)
	if err != nil {
		return fmt.Errorf("health-check request: %w", err)
	}
	defer response.Body.Close()
	if response.StatusCode != http.StatusOK {
		return fmt.Errorf("health-check status: %s", response.Status)
	}
	return nil
}

func (s *service) health(w http.ResponseWriter, _ *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	_, _ = w.Write([]byte("{\"status\":\"ok\"}\n"))
}

func (s *service) metrics(w http.ResponseWriter, _ *http.Request) {
	activeRooms, activeScreenPublishers := s.roomGauges()
	w.Header().Set("Content-Type", "text/plain; version=0.0.4")
	_, _ = fmt.Fprintf(
		w,
		"catro_signaling_active_connections %d\n"+
			"catro_signaling_active_rooms %d\n"+
			"catro_signaling_active_screen_publishers %d\n"+
			"catro_signaling_messages_total %d\n"+
			"catro_signaling_rejected_total %d\n"+
			"catro_signaling_room_capacity_rejections_total %d\n"+
			"catro_signaling_screen_claims_total %d\n"+
			"catro_signaling_screen_releases_total %d\n"+
			"catro_signaling_screen_busy_rejections_total %d\n"+
			"catro_signaling_api_rate_limit_rejections_total %d\n"+
			"catro_signaling_discovery_rate_limit_rejections_total %d\n",
		s.activeConnections.Load(),
		activeRooms,
		activeScreenPublishers,
		s.signalMessages.Load(),
		s.rejectedMessages.Load(),
		s.roomCapacityRejections.Load(),
		s.screenClaims.Load(),
		s.screenReleases.Load(),
		s.screenBusyRejections.Load(),
		s.apiRateLimitRejections.Load(),
		s.discoveryRateLimitRejections.Load())
}

func (s *service) roomGauges() (int, int) {
	s.roomsMu.Lock()
	defer s.roomsMu.Unlock()

	activeScreenPublishers := 0
	for _, rm := range s.rooms {
		rm.mu.RLock()
		if rm.screenOwner != "" {
			activeScreenPublishers++
		}
		rm.mu.RUnlock()
	}
	return len(s.rooms), activeScreenPublishers
}

func (s *service) websocket(w http.ResponseWriter, r *http.Request) {
	upgrader := websocket.Upgrader{
		HandshakeTimeout: joinTimeout,
		CheckOrigin: func(req *http.Request) bool {
			origin := req.Header.Get("Origin")
			if origin == "" {
				return true
			}
			return s.allowedOrigin != "" &&
				hmac.Equal([]byte(origin), []byte(s.allowedOrigin))
		},
	}
	conn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		return
	}
	s.activeConnections.Add(1)
	defer s.activeConnections.Add(-1)
	defer conn.Close()

	conn.SetReadLimit(maxSignalBytes)
	_ = conn.SetReadDeadline(time.Now().Add(joinTimeout))

	var join message
	if err := conn.ReadJSON(&join); err != nil || join.Type != "join" {
		s.rejectedMessages.Add(1)
		_ = writeJSON(conn, message{Type: "error", Message: "join required"})
		return
	}
	if join.Protocol != 1 || !validID(join.ServerID) ||
		!validID(join.ChannelID) || !validID(join.PeerID) {
		s.rejectedMessages.Add(1)
		_ = writeJSON(conn, message{Type: "error", Message: "invalid join request"})
		return
	}
	if err := verifyToken(s.secret, join.Token, claims{
		ServerID: join.ServerID, ChannelID: join.ChannelID, PeerID: join.PeerID,
	}); err != nil {
		s.rejectedMessages.Add(1)
		_ = writeJSON(conn, message{Type: "error", Message: "unauthorized"})
		return
	}

	rm := s.roomFor(join.ServerID, join.ChannelID)
	c := &client{id: join.PeerID, room: rm, conn: conn, rateStarted: time.Now()}
	existing, err := rm.add(c)
	if err != nil {
		s.rejectedMessages.Add(1)
		if errors.Is(err, errRoomCapacity) {
			s.roomCapacityRejections.Add(1)
		}
		_ = c.write(message{Type: "error", Message: err.Error()})
		return
	}
	defer func() {
		_, releasedScreen := rm.remove(c.id)
		rm.broadcastExcept(c.id, message{Type: "peer_left", PeerID: c.id})
		if releasedScreen {
			s.screenReleases.Add(1)
			rm.broadcast(message{Type: "screen_state"})
		}
		s.dropEmptyRoom(rm)
	}()

	if err := c.write(message{
		Type: "joined", Peers: existing, Protocol: 1,
		ScreenOwner: rm.currentScreenOwner(),
	}); err != nil {
		return
	}
	rm.broadcastExcept(c.id, message{Type: "peer_joined", PeerID: c.id})

	_ = conn.SetReadDeadline(time.Now().Add(readTimeout))
	conn.SetPongHandler(func(string) error {
		return conn.SetReadDeadline(time.Now().Add(readTimeout))
	})

	done := make(chan struct{})
	defer close(done)
	go func() {
		ticker := time.NewTicker(pingInterval)
		defer ticker.Stop()
		for {
			select {
			case <-done:
				return
			case <-ticker.C:
				c.writeMu.Lock()
				_ = conn.SetWriteDeadline(time.Now().Add(writeTimeout))
				err := conn.WriteMessage(websocket.PingMessage, nil)
				c.writeMu.Unlock()
				if err != nil {
					return
				}
			}
		}
	}()

	for {
		var incoming message
		if err := conn.ReadJSON(&incoming); err != nil {
			return
		}
		if !c.allowSignal() {
			s.rejectedMessages.Add(1)
			_ = c.write(message{Type: "error", Message: "signal rate exceeded"})
			return
		}
		switch incoming.Type {
		case "screen_claim":
			owner, acquired := rm.claimScreen(c.id)
			if acquired {
				s.screenClaims.Add(1)
				rm.broadcast(message{
					Type:        "screen_state",
					ScreenOwner: owner,
				})
			} else {
				s.screenBusyRejections.Add(1)
				_ = c.write(message{
					Type:        "screen_busy",
					ScreenOwner: owner,
				})
			}
			continue
		case "screen_release":
			if rm.releaseScreen(c.id) {
				s.screenReleases.Add(1)
				rm.broadcast(message{Type: "screen_state"})
			}
			continue
		case "signal":
		default:
			s.rejectedMessages.Add(1)
			continue
		}

		if !validID(incoming.To) ||
			(incoming.Kind != "offer" && incoming.Kind != "answer" &&
				incoming.Kind != "candidate") || incoming.To == c.id {
			s.rejectedMessages.Add(1)
			continue
		}
		switch incoming.Kind {
		case "offer", "answer":
			if len(incoming.SDP) == 0 || len(incoming.SDP) > maxSignalBytes {
				s.rejectedMessages.Add(1)
				continue
			}
			incoming.Candidate, incoming.MID = "", ""
		case "candidate":
			if len(incoming.Candidate) == 0 || len(incoming.Candidate) > maxCandidateBytes ||
				len(incoming.MID) > 256 {
				s.rejectedMessages.Add(1)
				continue
			}
			incoming.SDP = ""
		}

		// The authenticated socket owns identity. Never forward caller-supplied identity fields.
		incoming.Token, incoming.ServerID, incoming.ChannelID, incoming.PeerID = "", "", "", ""
		incoming.From, incoming.Protocol = c.id, 0

		if rm.forward(c.id, incoming.To, incoming) {
			s.signalMessages.Add(1)
		} else {
			s.rejectedMessages.Add(1)
		}
	}
}

func (s *service) roomFor(serverID, channelID string) *room {
	key := serverID + "/" + channelID
	s.roomsMu.Lock()
	defer s.roomsMu.Unlock()
	if existing := s.rooms[key]; existing != nil {
		return existing
	}
	rm := &room{key: key, peers: make(map[string]*client), maxPeers: s.maxRoomPeers}
	s.rooms[key] = rm
	return rm
}

func (s *service) voicePeers(serverID, channelID string) map[string]bool {
	s.roomsMu.Lock()
	rm := s.rooms[serverID+"/"+channelID]
	s.roomsMu.Unlock()
	present := make(map[string]bool)
	if rm != nil {
		rm.mu.RLock()
		for id := range rm.peers {
			present[id] = true
		}
		rm.mu.RUnlock()
	}
	return present
}

func (s *service) dropEmptyRoom(rm *room) {
	rm.mu.RLock()
	empty := len(rm.peers) == 0
	rm.mu.RUnlock()
	if !empty {
		return
	}
	s.roomsMu.Lock()
	defer s.roomsMu.Unlock()
	if s.rooms[rm.key] == rm {
		delete(s.rooms, rm.key)
	}
}

func (r *room) add(c *client) ([]string, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	if _, exists := r.peers[c.id]; exists {
		return nil, errors.New("peer already joined")
	}
	if len(r.peers) >= r.maxPeers {
		return nil, errRoomCapacity
	}
	existing := make([]string, 0, len(r.peers))
	for id := range r.peers {
		existing = append(existing, id)
	}
	r.peers[c.id] = c
	return existing, nil
}

func (r *room) remove(peerID string) (bool, bool) {
	r.mu.Lock()
	_, removed := r.peers[peerID]
	delete(r.peers, peerID)
	releasedScreen := r.screenOwner == peerID
	if releasedScreen {
		r.screenOwner = ""
	}
	r.mu.Unlock()
	return removed, releasedScreen
}

func (r *room) currentScreenOwner() string {
	r.mu.RLock()
	defer r.mu.RUnlock()
	return r.screenOwner
}

func (r *room) claimScreen(peerID string) (string, bool) {
	r.mu.Lock()
	defer r.mu.Unlock()
	if r.peers[peerID] == nil {
		return r.screenOwner, false
	}
	if r.screenOwner == "" {
		r.screenOwner = peerID
	}
	return r.screenOwner, r.screenOwner == peerID
}

func (r *room) releaseScreen(peerID string) bool {
	r.mu.Lock()
	defer r.mu.Unlock()
	if r.screenOwner != peerID {
		return false
	}
	r.screenOwner = ""
	return true
}

func (r *room) forward(from, to string, m message) bool {
	r.mu.RLock()
	target := r.peers[to]
	sourceExists := r.peers[from] != nil
	r.mu.RUnlock()
	return target != nil && sourceExists && target.write(m) == nil
}

func (r *room) broadcastExcept(except string, m message) {
	r.mu.RLock()
	peers := make([]*client, 0, len(r.peers))
	for id, peer := range r.peers {
		if id != except {
			peers = append(peers, peer)
		}
	}
	r.mu.RUnlock()
	for _, peer := range peers {
		_ = peer.write(m)
	}
}

func (r *room) broadcast(m message) {
	r.broadcastExcept("", m)
}

func (c *client) write(m message) error {
	c.writeMu.Lock()
	defer c.writeMu.Unlock()
	_ = c.conn.SetWriteDeadline(time.Now().Add(writeTimeout))
	return c.conn.WriteJSON(m)
}

func (c *client) allowSignal() bool {
	c.rateMu.Lock()
	defer c.rateMu.Unlock()
	now := time.Now()
	if now.Sub(c.rateStarted) >= signalWindow {
		c.rateStarted, c.rateCount = now, 0
	}
	c.rateCount++
	return c.rateCount <= maxSignalsPerWindow
}

func validID(value string) bool {
	if value == "" || len(value) > maxIDBytes {
		return false
	}
	for _, ch := range value {
		if !(ch >= 'a' && ch <= 'z') &&
			!(ch >= 'A' && ch <= 'Z') &&
			!(ch >= '0' && ch <= '9') &&
			ch != '-' && ch != '_' && ch != ':' {
			return false
		}
	}
	return true
}

func mintToken(secret []byte, value claims) (string, error) {
	if len(secret) < 32 || !validID(value.ServerID) || !validID(value.ChannelID) ||
		!validID(value.PeerID) || value.Expires <= time.Now().Unix() {
		return "", errors.New("invalid token claims")
	}
	raw, err := json.Marshal(value)
	if err != nil {
		return "", err
	}
	payload := base64.RawURLEncoding.EncodeToString(raw)
	mac := hmac.New(sha256.New, secret)
	_, _ = mac.Write([]byte("v1." + payload))
	sig := hex.EncodeToString(mac.Sum(nil))
	return "v1." + payload + "." + sig, nil
}

func verifyToken(secret []byte, token string, expected claims) error {
	return verifyTokenAt(secret, token, expected, time.Now())
}

func verifyTokenAt(secret []byte, token string, expected claims, now time.Time) error {
	parts := strings.Split(token, ".")
	if len(parts) != 3 || parts[0] != "v1" {
		return errors.New("bad token")
	}
	signature, err := hex.DecodeString(parts[2])
	if err != nil || len(signature) != sha256.Size {
		return errors.New("bad signature")
	}
	mac := hmac.New(sha256.New, secret)
	_, _ = mac.Write([]byte("v1." + parts[1]))
	if !hmac.Equal(signature, mac.Sum(nil)) {
		return errors.New("signature mismatch")
	}
	raw, err := base64.RawURLEncoding.DecodeString(parts[1])
	if err != nil {
		return err
	}
	var got claims
	if err := json.Unmarshal(raw, &got); err != nil {
		return err
	}
	if got.Expires <= now.Unix() ||
		got.ServerID != expected.ServerID ||
		got.ChannelID != expected.ChannelID ||
		got.PeerID != expected.PeerID {
		return errors.New("claims mismatch")
	}
	return nil
}

func writeJSON(conn *websocket.Conn, value any) error {
	_ = conn.SetWriteDeadline(time.Now().Add(writeTimeout))
	return conn.WriteJSON(value)
}
