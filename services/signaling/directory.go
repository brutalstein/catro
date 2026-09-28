package main

import (
	"crypto/hmac"
	"crypto/rand"
	"crypto/sha1"
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"
)

const (
	maxDirectoryBodyBytes = 32 * 1024
	maxDisplayNameBytes    = 64
	maxServerNameBytes     = 80
	defaultInviteTTL       = 7 * 24 * time.Hour
	maxInviteTTL           = 30 * 24 * time.Hour
	defaultRTCTokenTTL     = 15 * time.Minute
	maxDirectoryUsers      = 4096
	maxDirectoryServers    = 1024
	maxDirectoryInvites    = 4096
	maxInvitesPerServer    = 32
	maxDirectoryMembers    = 256
	maxDirectoryStateBytes = 32 * 1024 * 1024
)

type directoryUser struct {
	ID          string `json:"id"`
	DisplayName string `json:"display_name"`
	TokenHash   string `json:"token_hash"`
}

type directoryMember struct {
	UserID string `json:"user_id"`
	Role   string `json:"role"`
}

type directoryServer struct {
	ID             string                     `json:"id"`
	OwnerID        string                     `json:"owner_id"`
	Name           string                     `json:"name"`
	VoiceChannelID string                     `json:"voice_channel_id"`
	Members        map[string]directoryMember `json:"members"`
}

type directoryInvite struct {
	Code      string `json:"code"`
	ServerID  string `json:"server_id"`
	CreatorID string `json:"creator_id"`
	Expires   int64  `json:"expires"`
}

type directoryState struct {
	Users   map[string]directoryUser   `json:"users"`
	Servers map[string]directoryServer `json:"servers"`
	Invites map[string]directoryInvite `json:"invites"`
}

type directory struct {
	mu     sync.Mutex
	path   string
	secret []byte
	state  directoryState

	signalingURL             string
	iceServers               []string
	allowInsecureSignaling bool
	allowNoTURN            bool
	turnSecret             []byte
}

type serverDescriptor struct {
	ID             string `json:"id"`
	OwnerID        string `json:"owner_id"`
	Name           string `json:"name"`
	VoiceChannelID string `json:"voice_channel_id"`
	Role           string `json:"role"`
	MemberCount    int    `json:"member_count"`
}

func (d *directory) setRTCProvisioning(
	signalingURL string,
	iceServers []string,
	allowInsecure bool,
	allowNoTURN bool,
	turnSecret []byte,
) {
	d.mu.Lock()
	defer d.mu.Unlock()
	d.signalingURL = signalingURL
	d.iceServers = append([]string(nil), iceServers...)
	d.allowInsecureSignaling = allowInsecure
	d.allowNoTURN = allowNoTURN
	d.turnSecret = append([]byte(nil), turnSecret...)
}

func ephemeralICEServers(
	base []string,
	turnSecret []byte,
	peerID string,
	expires int64,
) ([]string, error) {
	result := make([]string, 0, len(base))
	username := fmt.Sprintf("%d:%s", expires, peerID)

	var password string
	if len(turnSecret) != 0 {
		mac := hmac.New(sha1.New, turnSecret)
		_, _ = mac.Write([]byte(username))
		password = base64.StdEncoding.EncodeToString(mac.Sum(nil))
	}

	for _, raw := range base {
		lower := strings.ToLower(raw)
		isTURN := strings.HasPrefix(lower, "turn:") ||
			strings.HasPrefix(lower, "turns:")
		if !isTURN {
			result = append(result, raw)
			continue
		}
		if len(turnSecret) == 0 {
			return nil, errors.New("TURN REST secret is not configured")
		}

		// net/url treats RFC 7065's turn:host form as opaque. Normalize only for credential
		// injection; libdatachannel accepts the resulting authority-form TURN URL.
		normalized := raw
		if strings.HasPrefix(lower, "turn:") &&
			!strings.HasPrefix(lower, "turn://") {
			normalized = "turn://" + raw[len("turn:"):]
		} else if strings.HasPrefix(lower, "turns:") &&
			!strings.HasPrefix(lower, "turns://") {
			normalized = "turns://" + raw[len("turns:"):]
		}

		parsed, err := url.Parse(normalized)
		if err != nil || parsed.Host == "" {
			return nil, fmt.Errorf("invalid TURN URL: %s", raw)
		}
		parsed.User = url.UserPassword(username, password)
		result = append(result, parsed.String())
	}
	return result, nil
}

func openDirectory(path string, secret []byte) (*directory, error) {
	if path == "" {
		return nil, errors.New("directory state file is required")
	}
	d := &directory{
		path:   path,
		secret: append([]byte(nil), secret...),
		state: directoryState{
			Users:   make(map[string]directoryUser),
			Servers: make(map[string]directoryServer),
			Invites: make(map[string]directoryInvite),
		},
	}
	raw, err := os.ReadFile(path)
	if errors.Is(err, os.ErrNotExist) {
		return d, nil
	}
	if err != nil {
		return nil, fmt.Errorf("read directory state: %w", err)
	}
	if len(raw) == 0 {
		return d, nil
	}
	if len(raw) > maxDirectoryStateBytes {
		return nil, errors.New("directory state exceeds configured bound")
	}
	if err := json.Unmarshal(raw, &d.state); err != nil {
		return nil, fmt.Errorf("decode directory state: %w", err)
	}
	if d.state.Users == nil {
		d.state.Users = make(map[string]directoryUser)
	}
	if d.state.Servers == nil {
		d.state.Servers = make(map[string]directoryServer)
	}
	if d.state.Invites == nil {
		d.state.Invites = make(map[string]directoryInvite)
	}
	if len(d.state.Users) > maxDirectoryUsers ||
		len(d.state.Servers) > maxDirectoryServers ||
		len(d.state.Invites) > maxDirectoryInvites {
		return nil, errors.New("directory state exceeds object-count bounds")
	}
	for id, server := range d.state.Servers {
		if server.Members == nil {
			server.Members = make(map[string]directoryMember)
			d.state.Servers[id] = server
		}
		if len(server.Members) > maxDirectoryMembers {
			return nil, errors.New("directory server exceeds member bound")
		}
	}
	return d, nil
}

func cloneMembers(source map[string]directoryMember) map[string]directoryMember {
	result := make(map[string]directoryMember, len(source))
	for id, member := range source {
		result[id] = member
	}
	return result
}

func (d *directory) pruneExpiredInvitesLocked(now int64) {
	for code, invite := range d.state.Invites {
		if invite.Expires <= now {
			delete(d.state.Invites, code)
		}
	}
}

func (d *directory) persistLocked() error {
	parent := filepath.Dir(d.path)
	if parent != "." && parent != "" {
		if err := os.MkdirAll(parent, 0o700); err != nil {
			return err
		}
	}
	raw, err := json.MarshalIndent(d.state, "", "  ")
	if err != nil {
		return err
	}
	tmp := d.path + ".tmp"
	file, err := os.OpenFile(tmp, os.O_CREATE|os.O_TRUNC|os.O_WRONLY, 0o600)
	if err != nil {
		return err
	}
	ok := false
	defer func() {
		_ = file.Close()
		if !ok {
			_ = os.Remove(tmp)
		}
	}()
	if _, err := file.Write(raw); err != nil {
		return err
	}
	if err := file.Sync(); err != nil {
		return err
	}
	if err := file.Close(); err != nil {
		return err
	}
	if err := os.Rename(tmp, d.path); err != nil {
		return err
	}
	ok = true
	return nil
}

func decodeJSON(w http.ResponseWriter, r *http.Request, destination any) bool {
	r.Body = http.MaxBytesReader(w, r.Body, maxDirectoryBodyBytes)
	decoder := json.NewDecoder(r.Body)
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(destination); err != nil {
		writeAPIError(w, http.StatusBadRequest, "invalid request")
		return false
	}
	var extra any
	if err := decoder.Decode(&extra); err != io.EOF {
		writeAPIError(w, http.StatusBadRequest, "invalid request")
		return false
	}
	return true
}

func writeAPIJSON(w http.ResponseWriter, status int, value any) {
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Cache-Control", "no-store")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(value)
}

func writeAPIError(w http.ResponseWriter, status int, message string) {
	writeAPIJSON(w, status, map[string]string{"error": message})
}

func validBoundedText(value string, maximum int) bool {
	if value == "" || len(value) > maximum {
		return false
	}
	for _, ch := range value {
		if ch < 0x20 || ch == 0x7f {
			return false
		}
	}
	return true
}

func credentialHash(encoded string) (string, bool) {
	raw, err := base64.RawURLEncoding.DecodeString(encoded)
	if err != nil || len(raw) != 32 {
		return "", false
	}
	sum := sha256.Sum256(raw)
	return hex.EncodeToString(sum[:]), true
}

func bearerToken(userID, credential string) string {
	return userID + "." + credential
}

func parseBearer(header string) (string, string, bool) {
	const prefix = "Bearer "
	if !strings.HasPrefix(header, prefix) {
		return "", "", false
	}
	token := strings.TrimSpace(strings.TrimPrefix(header, prefix))
	separator := strings.IndexByte(token, '.')
	if separator <= 0 || separator+1 >= len(token) {
		return "", "", false
	}
	userID := token[:separator]
	credential := token[separator+1:]
	if !validID(userID) {
		return "", "", false
	}
	return userID, credential, true
}

func (d *directory) authenticate(r *http.Request) (directoryUser, bool) {
	userID, credential, ok := parseBearer(r.Header.Get("Authorization"))
	if !ok {
		return directoryUser{}, false
	}
	hash, ok := credentialHash(credential)
	if !ok {
		return directoryUser{}, false
	}
	d.mu.Lock()
	user, exists := d.state.Users[userID]
	d.mu.Unlock()
	if !exists {
		return directoryUser{}, false
	}
	expected, err1 := hex.DecodeString(user.TokenHash)
	actual, err2 := hex.DecodeString(hash)
	if err1 != nil || err2 != nil || !hmac.Equal(expected, actual) {
		return directoryUser{}, false
	}
	return user, true
}

func (d *directory) handleRegister(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		w.Header().Set("Allow", http.MethodPost)
		writeAPIError(w, http.StatusMethodNotAllowed, "method not allowed")
		return
	}
	var request struct {
		UserID      string `json:"user_id"`
		DisplayName string `json:"display_name"`
		Credential  string `json:"credential"`
	}
	if !decodeJSON(w, r, &request) {
		return
	}
	hash, ok := credentialHash(request.Credential)
	if !validID(request.UserID) ||
		!validBoundedText(request.DisplayName, maxDisplayNameBytes) ||
		!ok {
		writeAPIError(w, http.StatusBadRequest, "invalid identity")
		return
	}

	d.mu.Lock()
	defer d.mu.Unlock()
	if existing, exists := d.state.Users[request.UserID]; exists {
		if existing.TokenHash != hash {
			writeAPIError(w, http.StatusConflict, "identity already registered")
			return
		}
		if existing.DisplayName != request.DisplayName {
			previous := existing
			existing.DisplayName = request.DisplayName
			d.state.Users[request.UserID] = existing
			if err := d.persistLocked(); err != nil {
				d.state.Users[request.UserID] = previous
				writeAPIError(w, http.StatusInternalServerError, "state persistence failed")
				return
			}
		}
		writeAPIJSON(w, http.StatusOK, map[string]string{
			"user_id": request.UserID,
			"access_token": bearerToken(request.UserID, request.Credential),
		})
		return
	}

	if len(d.state.Users) >= maxDirectoryUsers {
		writeAPIError(w, http.StatusServiceUnavailable, "directory user capacity reached")
		return
	}

	d.state.Users[request.UserID] = directoryUser{
		ID: request.UserID, DisplayName: request.DisplayName, TokenHash: hash,
	}
	if err := d.persistLocked(); err != nil {
		delete(d.state.Users, request.UserID)
		writeAPIError(w, http.StatusInternalServerError, "state persistence failed")
		return
	}
	writeAPIJSON(w, http.StatusCreated, map[string]string{
		"user_id": request.UserID,
		"access_token": bearerToken(request.UserID, request.Credential),
	})
}

func (d *directory) handleServerSync(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		w.Header().Set("Allow", http.MethodPost)
		writeAPIError(w, http.StatusMethodNotAllowed, "method not allowed")
		return
	}
	user, ok := d.authenticate(r)
	if !ok {
		writeAPIError(w, http.StatusUnauthorized, "unauthorized")
		return
	}
	var request struct {
		ServerID       string `json:"server_id"`
		Name           string `json:"name"`
		VoiceChannelID string `json:"voice_channel_id"`
	}
	if !decodeJSON(w, r, &request) {
		return
	}
	if !validID(request.ServerID) ||
		!validID(request.VoiceChannelID) ||
		!validBoundedText(request.Name, maxServerNameBytes) {
		writeAPIError(w, http.StatusBadRequest, "invalid server")
		return
	}

	d.mu.Lock()
	defer d.mu.Unlock()
	if existing, exists := d.state.Servers[request.ServerID]; exists {
		previous := existing
		member, memberExists := existing.Members[user.ID]
		if !memberExists || member.Role != "owner" || existing.OwnerID != user.ID {
			writeAPIError(w, http.StatusForbidden, "server owner required")
			return
		}
		existing.Name = request.Name
		existing.VoiceChannelID = request.VoiceChannelID
		d.state.Servers[request.ServerID] = existing
		if err := d.persistLocked(); err != nil {
			d.state.Servers[request.ServerID] = previous
			writeAPIError(w, http.StatusInternalServerError, "state persistence failed")
			return
		}
		writeAPIJSON(w, http.StatusOK, descriptorFor(existing, user.ID))
		return
	}

	if len(d.state.Servers) >= maxDirectoryServers {
		writeAPIError(w, http.StatusServiceUnavailable, "directory server capacity reached")
		return
	}

	server := directoryServer{
		ID: request.ServerID,
		OwnerID: user.ID,
		Name: request.Name,
		VoiceChannelID: request.VoiceChannelID,
		Members: map[string]directoryMember{
			user.ID: {UserID: user.ID, Role: "owner"},
		},
	}
	d.state.Servers[server.ID] = server
	if err := d.persistLocked(); err != nil {
		delete(d.state.Servers, server.ID)
		writeAPIError(w, http.StatusInternalServerError, "state persistence failed")
		return
	}
	writeAPIJSON(w, http.StatusCreated, descriptorFor(server, user.ID))
}

func descriptorFor(server directoryServer, userID string) serverDescriptor {
	role := "member"
	if member, ok := server.Members[userID]; ok && member.Role != "" {
		role = member.Role
	}
	return serverDescriptor{
		ID: server.ID,
		OwnerID: server.OwnerID,
		Name: server.Name,
		VoiceChannelID: server.VoiceChannelID,
		Role: role,
		MemberCount: len(server.Members),
	}
}

func (d *directory) handleServers(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		w.Header().Set("Allow", http.MethodGet)
		writeAPIError(w, http.StatusMethodNotAllowed, "method not allowed")
		return
	}
	user, ok := d.authenticate(r)
	if !ok {
		writeAPIError(w, http.StatusUnauthorized, "unauthorized")
		return
	}
	d.mu.Lock()
	servers := make([]serverDescriptor, 0)
	for _, server := range d.state.Servers {
		if _, member := server.Members[user.ID]; member {
			servers = append(servers, descriptorFor(server, user.ID))
		}
	}
	d.mu.Unlock()
	writeAPIJSON(w, http.StatusOK, map[string]any{"servers": servers})
}

func randomInviteCode() (string, error) {
	raw := make([]byte, 18)
	if _, err := rand.Read(raw); err != nil {
		return "", err
	}
	return base64.RawURLEncoding.EncodeToString(raw), nil
}

func (d *directory) handleInvites(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		w.Header().Set("Allow", http.MethodPost)
		writeAPIError(w, http.StatusMethodNotAllowed, "method not allowed")
		return
	}
	user, ok := d.authenticate(r)
	if !ok {
		writeAPIError(w, http.StatusUnauthorized, "unauthorized")
		return
	}
	var request struct {
		ServerID   string `json:"server_id"`
		TTLSeconds int64  `json:"ttl_seconds,omitempty"`
	}
	if !decodeJSON(w, r, &request) {
		return
	}
	if !validID(request.ServerID) {
		writeAPIError(w, http.StatusBadRequest, "invalid server")
		return
	}
	ttl := defaultInviteTTL
	if request.TTLSeconds != 0 {
		ttl = time.Duration(request.TTLSeconds) * time.Second
	}
	if ttl <= 0 || ttl > maxInviteTTL {
		writeAPIError(w, http.StatusBadRequest, "invalid invite lifetime")
		return
	}

	d.mu.Lock()
	defer d.mu.Unlock()
	d.pruneExpiredInvitesLocked(time.Now().Unix())
	server, exists := d.state.Servers[request.ServerID]
	if !exists || server.OwnerID != user.ID {
		writeAPIError(w, http.StatusForbidden, "server owner required")
		return
	}
	if len(d.state.Invites) >= maxDirectoryInvites {
		writeAPIError(w, http.StatusTooManyRequests, "directory invite capacity reached")
		return
	}
	activeForServer := 0
	for _, invite := range d.state.Invites {
		if invite.ServerID == server.ID {
			activeForServer++
		}
	}
	if activeForServer >= maxInvitesPerServer {
		writeAPIError(w, http.StatusTooManyRequests, "server invite capacity reached")
		return
	}

	code, err := randomInviteCode()
	if err != nil {
		writeAPIError(w, http.StatusInternalServerError, "invite entropy failed")
		return
	}
	invite := directoryInvite{
		Code: code,
		ServerID: server.ID,
		CreatorID: user.ID,
		Expires: time.Now().Add(ttl).Unix(),
	}
	d.state.Invites[code] = invite
	if err := d.persistLocked(); err != nil {
		delete(d.state.Invites, code)
		writeAPIError(w, http.StatusInternalServerError, "state persistence failed")
		return
	}
	writeAPIJSON(w, http.StatusCreated, map[string]any{
		"code": code,
		"server": descriptorFor(server, user.ID),
		"expires": invite.Expires,
	})
}

func (d *directory) handleInviteAccept(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		w.Header().Set("Allow", http.MethodPost)
		writeAPIError(w, http.StatusMethodNotAllowed, "method not allowed")
		return
	}
	user, ok := d.authenticate(r)
	if !ok {
		writeAPIError(w, http.StatusUnauthorized, "unauthorized")
		return
	}
	var request struct {
		Code string `json:"code"`
	}
	if !decodeJSON(w, r, &request) {
		return
	}
	if request.Code == "" || len(request.Code) > 128 {
		writeAPIError(w, http.StatusBadRequest, "invalid invite")
		return
	}

	d.mu.Lock()
	defer d.mu.Unlock()
	invite, exists := d.state.Invites[request.Code]
	if !exists || invite.Expires <= time.Now().Unix() {
		if exists {
			delete(d.state.Invites, request.Code)
			_ = d.persistLocked()
		}
		writeAPIError(w, http.StatusNotFound, "invite not found")
		return
	}
	server, exists := d.state.Servers[invite.ServerID]
	if !exists {
		delete(d.state.Invites, request.Code)
		_ = d.persistLocked()
		writeAPIError(w, http.StatusNotFound, "server not found")
		return
	}
	if server.Members == nil {
		server.Members = make(map[string]directoryMember)
	}
	if _, alreadyMember := server.Members[user.ID]; !alreadyMember &&
		len(server.Members) >= maxDirectoryMembers {
		writeAPIError(w, http.StatusConflict, "server member capacity reached")
		return
	}

	previousMembers := cloneMembers(server.Members)
	server.Members[user.ID] = directoryMember{UserID: user.ID, Role: "member"}
	d.state.Servers[server.ID] = server
	delete(d.state.Invites, request.Code)
	if err := d.persistLocked(); err != nil {
		server.Members = previousMembers
		d.state.Servers[server.ID] = server
		d.state.Invites[request.Code] = invite
		writeAPIError(w, http.StatusInternalServerError, "state persistence failed")
		return
	}
	writeAPIJSON(w, http.StatusOK, descriptorFor(server, user.ID))
}

func (d *directory) handleRTCToken(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		w.Header().Set("Allow", http.MethodPost)
		writeAPIError(w, http.StatusMethodNotAllowed, "method not allowed")
		return
	}
	user, ok := d.authenticate(r)
	if !ok {
		writeAPIError(w, http.StatusUnauthorized, "unauthorized")
		return
	}
	var request struct {
		ServerID  string `json:"server_id"`
		ChannelID string `json:"channel_id"`
	}
	if !decodeJSON(w, r, &request) {
		return
	}
	if !validID(request.ServerID) || !validID(request.ChannelID) {
		writeAPIError(w, http.StatusBadRequest, "invalid room")
		return
	}

	d.mu.Lock()
	server, exists := d.state.Servers[request.ServerID]
	_, member := server.Members[user.ID]
	signalingURL := d.signalingURL
	baseICEServers := append([]string(nil), d.iceServers...)
	allowInsecure := d.allowInsecureSignaling
	allowNoTURN := d.allowNoTURN
	turnSecret := append([]byte(nil), d.turnSecret...)
	d.mu.Unlock()
	if !exists || !member || server.VoiceChannelID != request.ChannelID {
		writeAPIError(w, http.StatusForbidden, "room membership required")
		return
	}

	expires := time.Now().Add(defaultRTCTokenTTL).Unix()
	iceServers, err := ephemeralICEServers(
		baseICEServers,
		turnSecret,
		user.ID,
		expires)
	if err != nil {
		writeAPIError(w, http.StatusServiceUnavailable, "TURN provisioning unavailable")
		return
	}

	token, err := mintToken(d.secret, claims{
		ServerID: server.ID,
		ChannelID: server.VoiceChannelID,
		PeerID: user.ID,
		Expires: expires,
	})
	if err != nil {
		writeAPIError(w, http.StatusInternalServerError, "token mint failed")
		return
	}
	if signalingURL == "" || len(iceServers) == 0 {
		writeAPIError(w, http.StatusServiceUnavailable, "rtc provisioning unavailable")
		return
	}
	writeAPIJSON(w, http.StatusOK, map[string]any{
		"token": token,
		"expires": expires,
		"server_id": server.ID,
		"channel_id": server.VoiceChannelID,
		"peer_id": user.ID,
		"signaling_url": signalingURL,
		"ice_servers": iceServers,
		"allow_insecure_signaling": allowInsecure,
		"allow_no_turn": allowNoTURN,
	})
}
