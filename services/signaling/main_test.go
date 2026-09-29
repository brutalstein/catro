package main

import (
	"bytes"
	"crypto/hmac"
	"crypto/sha1"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"net/url"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/gorilla/websocket"
)

func TestTokenRoundTripAndClaimBinding(t *testing.T) {
	secret := []byte(strings.Repeat("s", 32))
	want := claims{
		ServerID:  "server-1",
		ChannelID: "voice-1",
		PeerID:    "user-1",
		Expires:   time.Now().Add(time.Hour).Unix(),
	}
	token, err := mintToken(secret, want)
	if err != nil {
		t.Fatal(err)
	}
	if err := verifyToken(secret, token, want); err != nil {
		t.Fatalf("valid token rejected: %v", err)
	}

	wrong := want
	wrong.PeerID = "user-2"
	if err := verifyToken(secret, token, wrong); err == nil {
		t.Fatal("token must be bound to peer identity")
	}
}

func TestTamperedAndExpiredTokensAreRejected(t *testing.T) {
	secret := []byte(strings.Repeat("x", 32))
	value := claims{
		ServerID:  "server-1",
		ChannelID: "voice-1",
		PeerID:    "user-1",
		Expires:   time.Now().Add(time.Hour).Unix(),
	}
	token, err := mintToken(secret, value)
	if err != nil {
		t.Fatal(err)
	}

	tampered := token[:len(token)-1] + "0"
	if err := verifyToken(secret, tampered, value); err == nil {
		t.Fatal("tampered token must be rejected")
	}

	expiredAt := time.Unix(value.Expires+1, 0)
	if err := verifyTokenAt(secret, token, value, expiredAt); err == nil {
		t.Fatal("expired token must be rejected")
	}
}

func TestIDsAreBoundedAndURLSafe(t *testing.T) {
	for _, good := range []string{"abc", "server-1", "voice_room:2", "USER_3"} {
		if !validID(good) {
			t.Fatalf("expected valid id: %q", good)
		}
	}
	for _, bad := range []string{"", "space here", "../escape", "x/y"} {
		if validID(bad) {
			t.Fatalf("expected invalid id: %q", bad)
		}
	}
}

func testCredential(fill byte) string {
	return base64.RawURLEncoding.EncodeToString(
		bytes.Repeat([]byte{fill}, 32))
}

func registerTestUser(
	t *testing.T,
	d *directory,
	userID string,
	credential string,
) string {
	t.Helper()
	body, err := json.Marshal(map[string]any{
		"user_id":      userID,
		"display_name": userID,
		"credential":   credential,
	})
	if err != nil {
		t.Fatal(err)
	}
	request := httptest.NewRequest(
		http.MethodPost,
		"/v1/users/register",
		bytes.NewReader(body))
	response := httptest.NewRecorder()
	d.handleRegister(response, request)
	if response.Code != http.StatusCreated &&
		response.Code != http.StatusOK {
		t.Fatalf("register %s: status %d body %s",
			userID, response.Code, response.Body.String())
	}
	var decoded struct {
		AccessToken string `json:"access_token"`
	}
	if err := json.Unmarshal(response.Body.Bytes(), &decoded); err != nil {
		t.Fatal(err)
	}
	if decoded.AccessToken == "" {
		t.Fatal("registration did not return access token")
	}
	return decoded.AccessToken
}

func authenticatedRequest(
	method string,
	path string,
	token string,
	body any,
) *http.Request {
	var encoded []byte
	if body != nil {
		encoded, _ = json.Marshal(body)
	}
	request := httptest.NewRequest(
		method, path, bytes.NewReader(encoded))
	request.Header.Set("Authorization", "Bearer "+token)
	return request
}

type testRoomMessage struct {
	Type        string   `json:"type"`
	PeerID      string   `json:"peer_id,omitempty"`
	ScreenOwner string   `json:"screen_owner,omitempty"`
	Message     string   `json:"message,omitempty"`
	Peers       []string `json:"peers,omitempty"`
}

func newTestRoomService(
	t *testing.T,
) (*service, *httptest.Server, string) {
	t.Helper()
	secret := strings.Repeat("w", 32)
	s := &service{
		secret:       []byte(secret),
		maxRoomPeers: 5,
		rooms:        make(map[string]*room),
	}
	mux := http.NewServeMux()
	mux.HandleFunc("/v1/rtc", s.websocket)
	server := httptest.NewServer(mux)
	t.Cleanup(server.Close)
	return s, server, secret
}

func joinTestRoomPeer(
	t *testing.T,
	server *httptest.Server,
	secret string,
	peerID string,
) (*websocket.Conn, testRoomMessage) {
	t.Helper()
	token, err := mintToken(
		[]byte(secret),
		claims{
			ServerID:  "server-1",
			ChannelID: "voice-1",
			PeerID:    peerID,
			Expires:   time.Now().Add(time.Minute).Unix(),
		})
	if err != nil {
		t.Fatal(err)
	}
	socketURL := "ws" +
		strings.TrimPrefix(server.URL, "http") +
		"/v1/rtc"
	conn, _, err := websocket.DefaultDialer.Dial(
		socketURL, nil)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		_ = conn.Close()
	})
	if err := conn.WriteJSON(map[string]any{
		"type":       "join",
		"protocol":   1,
		"token":      token,
		"server_id":  "server-1",
		"channel_id": "voice-1",
		"peer_id":    peerID,
	}); err != nil {
		t.Fatal(err)
	}

	var joined testRoomMessage
	if err := conn.ReadJSON(&joined); err != nil {
		t.Fatal(err)
	}
	if joined.Type != "joined" {
		t.Fatalf("first message = %q, want joined",
			joined.Type)
	}
	return conn, joined
}

func readTestRoomMessage(
	t *testing.T,
	conn *websocket.Conn,
	wantType string,
) testRoomMessage {
	t.Helper()
	if err := conn.SetReadDeadline(
		time.Now().Add(2 * time.Second)); err != nil {
		t.Fatal(err)
	}
	for {
		var incoming testRoomMessage
		if err := conn.ReadJSON(&incoming); err != nil {
			t.Fatalf("read %s: %v", wantType, err)
		}
		if incoming.Type == wantType {
			return incoming
		}
	}
}

func claimTestScreen(
	t *testing.T,
	conn *websocket.Conn,
) {
	t.Helper()
	if err := conn.WriteJSON(
		map[string]any{"type": "screen_claim"}); err != nil {
		t.Fatal(err)
	}
}

func TestScreenClaimIsExclusiveAndIdempotent(t *testing.T) {
	_, server, secret := newTestRoomService(t)
	owner, _ := joinTestRoomPeer(
		t, server, secret, "owner-1")
	guest, _ := joinTestRoomPeer(
		t, server, secret, "guest-1")

	claimTestScreen(t, owner)
	for _, conn := range []*websocket.Conn{owner, guest} {
		state := readTestRoomMessage(
			t, conn, "screen_state")
		if state.ScreenOwner != "owner-1" {
			t.Fatalf(
				"screen owner = %q, want owner-1",
				state.ScreenOwner)
		}
	}

	claimTestScreen(t, owner)
	state := readTestRoomMessage(
		t, owner, "screen_state")
	if state.ScreenOwner != "owner-1" {
		t.Fatalf(
			"idempotent owner = %q, want owner-1",
			state.ScreenOwner)
	}

	claimTestScreen(t, guest)
	busy := readTestRoomMessage(
		t, guest, "screen_busy")
	if busy.ScreenOwner != "owner-1" {
		t.Fatalf(
			"busy owner = %q, want owner-1",
			busy.ScreenOwner)
	}
}

func TestScreenOwnerReleaseAndDisconnectBroadcastState(
	t *testing.T,
) {
	_, server, secret := newTestRoomService(t)
	owner, _ := joinTestRoomPeer(
		t, server, secret, "owner-1")
	guest, _ := joinTestRoomPeer(
		t, server, secret, "guest-1")

	claimTestScreen(t, owner)
	_ = readTestRoomMessage(t, owner, "screen_state")
	_ = readTestRoomMessage(t, guest, "screen_state")

	if err := owner.WriteJSON(
		map[string]any{"type": "screen_release"}); err != nil {
		t.Fatal(err)
	}
	for _, conn := range []*websocket.Conn{owner, guest} {
		state := readTestRoomMessage(
			t, conn, "screen_state")
		if state.ScreenOwner != "" {
			t.Fatalf(
				"released owner = %q, want empty",
				state.ScreenOwner)
		}
	}

	claimTestScreen(t, owner)
	_ = readTestRoomMessage(t, owner, "screen_state")
	_ = readTestRoomMessage(t, guest, "screen_state")
	if err := owner.Close(); err != nil {
		t.Fatal(err)
	}
	state := readTestRoomMessage(
		t, guest, "screen_state")
	if state.ScreenOwner != "" {
		t.Fatalf(
			"disconnected owner = %q, want empty",
			state.ScreenOwner)
	}
}

func TestLateJoinReceivesCurrentScreenOwner(t *testing.T) {
	_, server, secret := newTestRoomService(t)
	owner, _ := joinTestRoomPeer(
		t, server, secret, "owner-1")
	claimTestScreen(t, owner)
	_ = readTestRoomMessage(t, owner, "screen_state")

	_, joined := joinTestRoomPeer(
		t, server, secret, "late-1")
	if joined.ScreenOwner != "owner-1" {
		t.Fatalf(
			"late join owner = %q, want owner-1",
			joined.ScreenOwner)
	}
}

func TestNonOwnerCannotReleaseScreen(t *testing.T) {
	_, server, secret := newTestRoomService(t)
	owner, _ := joinTestRoomPeer(
		t, server, secret, "owner-1")
	guest, _ := joinTestRoomPeer(
		t, server, secret, "guest-1")

	claimTestScreen(t, owner)
	_ = readTestRoomMessage(t, owner, "screen_state")
	_ = readTestRoomMessage(t, guest, "screen_state")

	if err := guest.WriteJSON(
		map[string]any{"type": "screen_release"}); err != nil {
		t.Fatal(err)
	}
	claimTestScreen(t, guest)
	busy := readTestRoomMessage(
		t, guest, "screen_busy")
	if busy.ScreenOwner != "owner-1" {
		t.Fatalf(
			"owner after non-owner release = %q, want owner-1",
			busy.ScreenOwner)
	}
}

func TestRoomCapacityRejectsSixthPeer(t *testing.T) {
	rm := &room{
		key:      "server-1/voice-1",
		peers:    make(map[string]*client),
		maxPeers: 5,
	}
	for index := 1; index <= 5; index++ {
		id := fmt.Sprintf("peer-%d", index)
		if _, err := rm.add(&client{id: id, room: rm}); err != nil {
			t.Fatalf("add %s: %v", id, err)
		}
	}

	if _, err := rm.add(&client{id: "peer-6", room: rm}); err == nil ||
		err.Error() != "room capacity reached" {
		t.Fatalf("sixth peer error = %v", err)
	}
}

func TestRoomCapacitySlotReturnsAfterDisconnect(t *testing.T) {
	rm := &room{
		key:      "server-1/voice-1",
		peers:    make(map[string]*client),
		maxPeers: 5,
	}
	for index := 1; index <= 5; index++ {
		id := fmt.Sprintf("peer-%d", index)
		if _, err := rm.add(&client{id: id, room: rm}); err != nil {
			t.Fatalf("add %s: %v", id, err)
		}
	}

	rm.remove("peer-3")
	if _, err := rm.add(&client{id: "peer-6", room: rm}); err != nil {
		t.Fatalf("replacement peer rejected: %v", err)
	}
}

func TestRTCProvisioningIncludesRoomCapacity(t *testing.T) {
	secret := []byte(strings.Repeat("c", 32))
	d, err := openDirectory(
		filepath.Join(t.TempDir(), "directory.json"),
		secret)
	if err != nil {
		t.Fatal(err)
	}
	d.setRTCProvisioning(
		"wss://rtc.example.test/v1/rtc",
		[]string{
			"stun:stun.example.test:3478",
			"turn:turn.example.test:3478?transport=udp",
		},
		false,
		false,
		[]byte(strings.Repeat("t", 32)),
		5)

	token := registerTestUser(
		t, d, "member-1", testCredential(0x61))
	d.mu.Lock()
	d.state.Servers["server-1"] = directoryServer{
		ID:             "server-1",
		OwnerID:        "member-1",
		Name:           "Test Server",
		VoiceChannelID: "voice-1",
		Members: map[string]directoryMember{
			"member-1": {
				UserID: "member-1",
				Role:   "owner",
			},
		},
	}
	d.mu.Unlock()

	request := authenticatedRequest(
		http.MethodPost,
		"/v1/rtc-token",
		token,
		map[string]any{
			"server_id":  "server-1",
			"channel_id": "voice-1",
		})
	response := httptest.NewRecorder()
	d.handleRTCToken(response, request)
	if response.Code != http.StatusOK {
		t.Fatalf("rtc token: %d %s",
			response.Code, response.Body.String())
	}

	var provisioning struct {
		MaxRoomPeers int `json:"max_room_peers"`
	}
	if err := json.Unmarshal(
		response.Body.Bytes(),
		&provisioning); err != nil {
		t.Fatal(err)
	}
	if provisioning.MaxRoomPeers != 5 {
		t.Fatalf(
			"max_room_peers = %d, want 5",
			provisioning.MaxRoomPeers)
	}
}

func TestDirectoryInviteMembershipAndRTCTokenFlow(t *testing.T) {
	secret := []byte(strings.Repeat("d", 32))
	d, err := openDirectory(
		filepath.Join(t.TempDir(), "directory.json"),
		secret)
	if err != nil {
		t.Fatal(err)
	}
	d.setRTCProvisioning(
		"wss://rtc.example.test/v1/rtc",
		[]string{
			"stun:stun.example.test:3478",
			"turn:turn.example.test:3478?transport=udp",
		},
		false,
		false,
		[]byte(strings.Repeat("t", 32)),
		5)

	ownerToken := registerTestUser(
		t, d, "owner-1", testCredential(0x11))
	memberToken := registerTestUser(
		t, d, "member-1", testCredential(0x22))
	outsiderToken := registerTestUser(
		t, d, "outsider-1", testCredential(0x33))

	serverRequest := authenticatedRequest(
		http.MethodPost,
		"/v1/servers/sync",
		ownerToken,
		map[string]any{
			"server_id":        "server-1",
			"name":             "Test Server",
			"voice_channel_id": "voice-1",
		})
	serverResponse := httptest.NewRecorder()
	d.handleServerSync(serverResponse, serverRequest)
	if serverResponse.Code != http.StatusCreated {
		t.Fatalf("sync server: %d %s",
			serverResponse.Code,
			serverResponse.Body.String())
	}

	inviteRequest := authenticatedRequest(
		http.MethodPost,
		"/v1/invites",
		ownerToken,
		map[string]any{"server_id": "server-1"})
	inviteResponse := httptest.NewRecorder()
	d.handleInvites(inviteResponse, inviteRequest)
	if inviteResponse.Code != http.StatusCreated {
		t.Fatalf("create invite: %d %s",
			inviteResponse.Code,
			inviteResponse.Body.String())
	}
	var invite struct {
		Code string `json:"code"`
	}
	if err := json.Unmarshal(
		inviteResponse.Body.Bytes(),
		&invite); err != nil {
		t.Fatal(err)
	}
	if invite.Code == "" {
		t.Fatal("invite code is empty")
	}

	acceptRequest := authenticatedRequest(
		http.MethodPost,
		"/v1/invites/accept",
		memberToken,
		map[string]any{"code": invite.Code})
	acceptResponse := httptest.NewRecorder()
	d.handleInviteAccept(acceptResponse, acceptRequest)
	if acceptResponse.Code != http.StatusOK {
		t.Fatalf("accept invite: %d %s",
			acceptResponse.Code,
			acceptResponse.Body.String())
	}

	replayRequest := authenticatedRequest(
		http.MethodPost,
		"/v1/invites/accept",
		outsiderToken,
		map[string]any{"code": invite.Code})
	replayResponse := httptest.NewRecorder()
	d.handleInviteAccept(replayResponse, replayRequest)
	if replayResponse.Code != http.StatusNotFound {
		t.Fatalf("invite replay status = %d",
			replayResponse.Code)
	}

	memberRTC := authenticatedRequest(
		http.MethodPost,
		"/v1/rtc-token",
		memberToken,
		map[string]any{
			"server_id":  "server-1",
			"channel_id": "voice-1",
		})
	memberRTCResponse := httptest.NewRecorder()
	d.handleRTCToken(memberRTCResponse, memberRTC)
	if memberRTCResponse.Code != http.StatusOK {
		t.Fatalf("member rtc token: %d %s",
			memberRTCResponse.Code,
			memberRTCResponse.Body.String())
	}
	var rtc struct {
		Token string `json:"token"`
	}
	if err := json.Unmarshal(
		memberRTCResponse.Body.Bytes(),
		&rtc); err != nil {
		t.Fatal(err)
	}
	if err := verifyToken(secret, rtc.Token, claims{
		ServerID:  "server-1",
		ChannelID: "voice-1",
		PeerID:    "member-1",
	}); err != nil {
		t.Fatalf("issued rtc token rejected: %v", err)
	}

	outsiderRTC := authenticatedRequest(
		http.MethodPost,
		"/v1/rtc-token",
		outsiderToken,
		map[string]any{
			"server_id":  "server-1",
			"channel_id": "voice-1",
		})
	outsiderRTCResponse := httptest.NewRecorder()
	d.handleRTCToken(outsiderRTCResponse, outsiderRTC)
	if outsiderRTCResponse.Code != http.StatusForbidden {
		t.Fatalf("outsider rtc token status = %d",
			outsiderRTCResponse.Code)
	}
}

func TestDirectoryRegistrationIsCredentialBoundAndPersistent(t *testing.T) {
	secret := []byte(strings.Repeat("p", 32))
	path := filepath.Join(t.TempDir(), "directory.json")
	d, err := openDirectory(path, secret)
	if err != nil {
		t.Fatal(err)
	}
	credential := testCredential(0x44)
	token := registerTestUser(t, d, "user-1", credential)

	retry := registerTestUser(t, d, "user-1", credential)
	if token != retry {
		t.Fatal("registration retry changed bearer token")
	}

	reloaded, err := openDirectory(path, secret)
	if err != nil {
		t.Fatal(err)
	}
	request := authenticatedRequest(
		http.MethodGet,
		"/v1/servers",
		token,
		nil)
	response := httptest.NewRecorder()
	reloaded.handleServers(response, request)
	if response.Code != http.StatusOK {
		t.Fatalf("reloaded authentication failed: %d",
			response.Code)
	}

	conflictBody, _ := json.Marshal(map[string]any{
		"user_id":      "user-1",
		"display_name": "user-1",
		"credential":   testCredential(0x55),
	})
	conflictRequest := httptest.NewRequest(
		http.MethodPost,
		"/v1/users/register",
		bytes.NewReader(conflictBody))
	conflictResponse := httptest.NewRecorder()
	reloaded.handleRegister(conflictResponse, conflictRequest)
	if conflictResponse.Code != http.StatusConflict {
		t.Fatalf("credential takeover status = %d",
			conflictResponse.Code)
	}
}

func TestEphemeralTurnCredentialsAreBoundAndShortLived(t *testing.T) {
	secret := []byte(strings.Repeat("q", 32))
	expires := time.Now().Add(15 * time.Minute).Unix()
	servers, err := ephemeralICEServers(
		[]string{
			"stun:stun.example.test:3478",
			"turn:turn.example.test:3478?transport=udp",
			"turns://turn.example.test:5349?transport=tls",
		},
		secret,
		"peer-1",
		expires)
	if err != nil {
		t.Fatal(err)
	}
	if len(servers) != 3 {
		t.Fatalf("ICE server count = %d", len(servers))
	}
	if servers[0] != "stun:stun.example.test:3478" {
		t.Fatalf("STUN URL changed: %s", servers[0])
	}

	wantUser := fmt.Sprintf("%d:peer-1", expires)
	for _, raw := range servers[1:] {
		parsed, err := url.Parse(raw)
		if err != nil {
			t.Fatal(err)
		}
		if parsed.User == nil {
			t.Fatalf("TURN URL has no credential: %s", raw)
		}
		gotUser := parsed.User.Username()
		gotPassword, ok := parsed.User.Password()
		if !ok {
			t.Fatalf("TURN URL has no password: %s", raw)
		}
		if gotUser != wantUser {
			t.Fatalf("TURN username = %q, want %q", gotUser, wantUser)
		}

		mac := hmac.New(sha1.New, secret)
		_, _ = mac.Write([]byte(wantUser))
		wantPassword := base64.StdEncoding.EncodeToString(mac.Sum(nil))
		if gotPassword != wantPassword {
			t.Fatal("TURN REST password mismatch")
		}
	}
}

func TestTurnProvisioningRequiresSecret(t *testing.T) {
	_, err := ephemeralICEServers(
		[]string{"turn:turn.example.test:3478"},
		nil,
		"peer-1",
		time.Now().Add(time.Minute).Unix())
	if err == nil {
		t.Fatal("TURN provisioning without secret must fail")
	}
}
