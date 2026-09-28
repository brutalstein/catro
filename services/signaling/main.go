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
	maxSignalBytes      = 64 * 1024
	maxCandidateBytes   = 4096
	maxIDBytes          = 128
	defaultMaxRoom      = 16
	writeTimeout        = 5 * time.Second
	readTimeout         = 45 * time.Second
	pingInterval        = 20 * time.Second
	joinTimeout         = 8 * time.Second
	signalWindow        = 10 * time.Second
	maxSignalsPerWindow = 256
)

type claims struct {
	ServerID  string `json:"server_id"`
	ChannelID string `json:"channel_id"`
	PeerID    string `json:"peer_id"`
	Expires   int64  `json:"exp"`
}

type message struct {
	Type      string   `json:"type"`
	Token     string   `json:"token,omitempty"`
	ServerID  string   `json:"server_id,omitempty"`
	ChannelID string   `json:"channel_id,omitempty"`
	PeerID    string   `json:"peer_id,omitempty"`
	Protocol  int      `json:"protocol,omitempty"`
	Peers     []string `json:"peers,omitempty"`
	To        string   `json:"to,omitempty"`
	From      string   `json:"from,omitempty"`
	Kind      string   `json:"kind,omitempty"`
	SDP       string   `json:"sdp,omitempty"`
	Candidate string   `json:"candidate,omitempty"`
	MID       string   `json:"mid,omitempty"`
	Message   string   `json:"message,omitempty"`
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
	key      string
	mu       sync.RWMutex
	peers    map[string]*client
	maxPeers int
}

type service struct {
	secret        []byte
	maxRoomPeers  int
	allowedOrigin string

	roomsMu sync.Mutex
	rooms   map[string]*room

	activeConnections atomic.Int64
	signalMessages    atomic.Uint64
	rejectedMessages  atomic.Uint64
}

func main() {
	addr := flag.String("addr", ":8443", "listen address")
	secret := flag.String("secret", os.Getenv("CATRO_SIGNALING_SECRET"), "HMAC secret")
	tlsCert := flag.String("tls-cert", os.Getenv("CATRO_SIGNALING_TLS_CERT"), "TLS certificate path")
	tlsKey := flag.String("tls-key", os.Getenv("CATRO_SIGNALING_TLS_KEY"), "TLS private key path")
	maxRoom := flag.Int("max-room-peers", defaultMaxRoom, "maximum peers per voice room")
	origin := flag.String("allowed-origin", "", "optional exact browser Origin")
	allowHTTP := flag.Bool("allow-insecure-http", false, "engineering only: plaintext HTTP/WebSocket")
	mint := flag.Bool("mint-token", false, "mint one room token and exit")
	serverID := flag.String("server-id", "", "token server id")
	channelID := flag.String("channel-id", "", "token voice channel id")
	peerID := flag.String("peer-id", "", "token peer/user id")
	ttl := flag.Duration("ttl", 24*time.Hour, "minted token lifetime")
	flag.Parse()

	if len(*secret) < 32 {
		log.Fatal("CATRO signaling secret must be at least 32 bytes")
	}
	if *maxRoom < 2 || *maxRoom > 64 {
		log.Fatal("max-room-peers must be between 2 and 64")
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

	s := &service{
		secret: []byte(*secret), maxRoomPeers: *maxRoom,
		allowedOrigin: *origin, rooms: make(map[string]*room),
	}

	mux := http.NewServeMux()
	mux.HandleFunc("/healthz", s.health)
	mux.HandleFunc("/metrics", s.metrics)
	mux.HandleFunc("/v1/rtc", s.websocket)

	httpServer := &http.Server{
		Addr: *addr, Handler: mux,
		ReadHeaderTimeout: 5 * time.Second,
		IdleTimeout: 60 * time.Second,
		MaxHeaderBytes: 16 * 1024,
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

func (s *service) health(w http.ResponseWriter, _ *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	_, _ = w.Write([]byte("{\"status\":\"ok\"}\n"))
}

func (s *service) metrics(w http.ResponseWriter, _ *http.Request) {
	s.roomsMu.Lock()
	rooms := len(s.rooms)
	s.roomsMu.Unlock()
	w.Header().Set("Content-Type", "text/plain; version=0.0.4")
	_, _ = fmt.Fprintf(w,
		"catro_signaling_active_connections %d\ncatro_signaling_rooms %d\ncatro_signaling_messages_total %d\ncatro_signaling_rejected_total %d\n",
		s.activeConnections.Load(), rooms, s.signalMessages.Load(), s.rejectedMessages.Load())
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
		_ = c.write(message{Type: "error", Message: err.Error()})
		return
	}
	defer func() {
		rm.remove(c.id)
		rm.broadcastExcept(c.id, message{Type: "peer_left", PeerID: c.id})
		s.dropEmptyRoom(rm)
	}()

	if err := c.write(message{Type: "joined", Peers: existing, Protocol: 1}); err != nil {
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
		if incoming.Type != "signal" || !validID(incoming.To) ||
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
		return nil, errors.New("room capacity reached")
	}
	existing := make([]string, 0, len(r.peers))
	for id := range r.peers {
		existing = append(existing, id)
	}
	r.peers[c.id] = c
	return existing, nil
}

func (r *room) remove(peerID string) {
	r.mu.Lock()
	delete(r.peers, peerID)
	r.mu.Unlock()
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
