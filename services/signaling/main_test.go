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

	replacement := byte('0')
	if token[len(token)-1] == replacement {
		replacement = '1'
	}
	tampered := token[:len(token)-1] + string(replacement)
	if tampered == token {
		t.Fatal("tamper fixture did not mutate token")
	}
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
	return registerTestUserNamed(
		t, d, userID, userID, credential)
}

func registerTestUserNamed(
	t *testing.T,
	d *directory,
	userID string,
	displayName string,
	credential string,
) string {
	t.Helper()
	body, err := json.Marshal(map[string]any{
		"user_id":      userID,
		"display_name": displayName,
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
			"text_channel_id":  "text-1",
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


func TestDirectoryTextMessagesAreAuthorizedPersistentAndCursorBounded(t *testing.T) {
	secret := []byte(strings.Repeat("m", 32))
	path := filepath.Join(t.TempDir(), "directory.json")
	d, err := openDirectory(path, secret)
	if err != nil {
		t.Fatal(err)
	}

	ownerToken := registerTestUser(t, d, "owner-1", testCredential(0x71))
	memberToken := registerTestUser(t, d, "member-1", testCredential(0x72))
	outsiderToken := registerTestUser(t, d, "outsider-1", testCredential(0x73))

	serverRequest := authenticatedRequest(
		http.MethodPost,
		"/v1/servers/sync",
		ownerToken,
		map[string]any{
			"server_id":        "server-1",
			"name":             "Text Server",
			"text_channel_id":  "text-1",
			"voice_channel_id": "voice-1",
		})
	serverResponse := httptest.NewRecorder()
	d.handleServerSync(serverResponse, serverRequest)
	if serverResponse.Code != http.StatusCreated {
		t.Fatalf("sync server: %d %s",
			serverResponse.Code, serverResponse.Body.String())
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
			inviteResponse.Code, inviteResponse.Body.String())
	}
	var invite struct {
		Code string `json:"code"`
	}
	if err := json.Unmarshal(inviteResponse.Body.Bytes(), &invite); err != nil {
		t.Fatal(err)
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
			acceptResponse.Code, acceptResponse.Body.String())
	}

	firstRequest := authenticatedRequest(
		http.MethodPost,
		"/v1/messages",
		ownerToken,
		map[string]any{
			"server_id":  "server-1",
			"channel_id": "text-1",
			"content":    "hello from owner",
		})
	firstResponse := httptest.NewRecorder()
	d.handleMessages(firstResponse, firstRequest)
	if firstResponse.Code != http.StatusCreated {
		t.Fatalf("send first message: %d %s",
			firstResponse.Code, firstResponse.Body.String())
	}
	var first messageDescriptor
	if err := json.Unmarshal(firstResponse.Body.Bytes(), &first); err != nil {
		t.Fatal(err)
	}
	if first.Sequence == 0 ||
		first.AuthorID != "owner-1" ||
		first.AuthorDisplayName != "owner-1" ||
		first.Content != "hello from owner" {
		t.Fatalf("unexpected first message: %#v", first)
	}

	outsiderRead := authenticatedRequest(
		http.MethodGet,
		"/v1/messages?server_id=server-1&channel_id=text-1",
		outsiderToken,
		nil)
	outsiderResponse := httptest.NewRecorder()
	d.handleMessages(outsiderResponse, outsiderRead)
	if outsiderResponse.Code != http.StatusForbidden {
		t.Fatalf("outsider read status = %d", outsiderResponse.Code)
	}

	secondRequest := authenticatedRequest(
		http.MethodPost,
		"/v1/messages",
		memberToken,
		map[string]any{
			"server_id":  "server-1",
			"channel_id": "text-1",
			"content":    "reply",
		})
	secondResponse := httptest.NewRecorder()
	d.handleMessages(secondResponse, secondRequest)
	if secondResponse.Code != http.StatusCreated {
		t.Fatalf("send second message: %d %s",
			secondResponse.Code, secondResponse.Body.String())
	}
	var second messageDescriptor
	if err := json.Unmarshal(secondResponse.Body.Bytes(), &second); err != nil {
		t.Fatal(err)
	}
	if second.Sequence <= first.Sequence {
		t.Fatalf("message sequence did not advance: %d -> %d",
			first.Sequence, second.Sequence)
	}

	listRequest := authenticatedRequest(
		http.MethodGet,
		fmt.Sprintf(
			"/v1/messages?server_id=server-1&channel_id=text-1&after=%d&limit=100",
			first.Sequence),
		memberToken,
		nil)
	listResponse := httptest.NewRecorder()
	d.handleMessages(listResponse, listRequest)
	if listResponse.Code != http.StatusOK {
		t.Fatalf("list messages: %d %s",
			listResponse.Code, listResponse.Body.String())
	}
	var page struct {
		Messages  []messageDescriptor `json:"messages"`
		NextAfter uint64              `json:"next_after"`
	}
	if err := json.Unmarshal(listResponse.Body.Bytes(), &page); err != nil {
		t.Fatal(err)
	}
	if len(page.Messages) != 1 ||
		page.Messages[0].ID != second.ID ||
		page.NextAfter != second.Sequence {
		t.Fatalf("unexpected cursor page: %#v", page)
	}

	reloaded, err := openDirectory(path, secret)
	if err != nil {
		t.Fatal(err)
	}
	reloadedRead := authenticatedRequest(
		http.MethodGet,
		"/v1/messages?server_id=server-1&channel_id=text-1&limit=100",
		memberToken,
		nil)
	reloadedResponse := httptest.NewRecorder()
	reloaded.handleMessages(reloadedResponse, reloadedRead)
	if reloadedResponse.Code != http.StatusOK {
		t.Fatalf("reloaded list: %d %s",
			reloadedResponse.Code, reloadedResponse.Body.String())
	}
	var reloadedPage struct {
		Messages []messageDescriptor `json:"messages"`
	}
	if err := json.Unmarshal(reloadedResponse.Body.Bytes(), &reloadedPage); err != nil {
		t.Fatal(err)
	}
	if len(reloadedPage.Messages) != 2 {
		t.Fatalf("reloaded message count = %d, want 2",
			len(reloadedPage.Messages))
	}
}

func TestDirectoryTextMessageRetentionDropsOldestInChannel(t *testing.T) {
	secret := []byte(strings.Repeat("n", 32))
	path := filepath.Join(t.TempDir(), "directory.json")
	d, err := openDirectory(path, secret)
	if err != nil {
		t.Fatal(err)
	}
	token := registerTestUser(t, d, "owner-1", testCredential(0x81))

	serverRequest := authenticatedRequest(
		http.MethodPost,
		"/v1/servers/sync",
		token,
		map[string]any{
			"server_id":        "server-1",
			"name":             "Retention Server",
			"text_channel_id":  "text-1",
			"voice_channel_id": "voice-1",
		})
	serverResponse := httptest.NewRecorder()
	d.handleServerSync(serverResponse, serverRequest)
	if serverResponse.Code != http.StatusCreated {
		t.Fatalf("sync server: %d %s",
			serverResponse.Code, serverResponse.Body.String())
	}

	d.mu.Lock()
	log := make([]directoryMessage, 0, maxMessagesPerChannel)
	for index := 1; index <= maxMessagesPerChannel; index++ {
		log = append(log, directoryMessage{
			ID:        fmt.Sprintf("message-%d", index),
			Sequence:  uint64(index),
			ServerID:  "server-1",
			ChannelID: "text-1",
			AuthorID:  "owner-1",
			Content:   "retained",
			CreatedAt: int64(index),
		})
	}
	d.state.Messages[messageKey("server-1", "text-1")] = log
	d.state.NextMessageSequence = maxMessagesPerChannel + 1
	if err := d.persistLocked(); err != nil {
		d.mu.Unlock()
		t.Fatal(err)
	}
	d.mu.Unlock()

	sendRequest := authenticatedRequest(
		http.MethodPost,
		"/v1/messages",
		token,
		map[string]any{
			"server_id":  "server-1",
			"channel_id": "text-1",
			"content":    "newest",
		})
	sendResponse := httptest.NewRecorder()
	d.handleMessages(sendResponse, sendRequest)
	if sendResponse.Code != http.StatusCreated {
		t.Fatalf("send retained message: %d %s",
			sendResponse.Code, sendResponse.Body.String())
	}

	d.mu.Lock()
	defer d.mu.Unlock()
	retained := d.state.Messages[messageKey("server-1", "text-1")]
	if len(retained) != maxMessagesPerChannel {
		t.Fatalf("retained message count = %d", len(retained))
	}
	if retained[0].Sequence != 2 ||
		retained[len(retained)-1].Content != "newest" {
		t.Fatalf("retention window is incorrect: first=%d last=%q",
			retained[0].Sequence,
			retained[len(retained)-1].Content)
	}
}


func TestMessageKeySeparatesColonBearingIdentifiers(t *testing.T) {
	left := messageKey("a:b", "c")
	right := messageKey("a", "b:c")
	if left == right {
		t.Fatalf("message key collision: %q", left)
	}
}


func TestServerSyncDoesNotOrphanTextHistoryByChangingChannelIdentity(t *testing.T) {
	secret := []byte(strings.Repeat("i", 32))
	d, err := openDirectory(
		filepath.Join(t.TempDir(), "directory.json"),
		secret)
	if err != nil {
		t.Fatal(err)
	}
	token := registerTestUser(t, d, "owner-1", testCredential(0x91))

	first := authenticatedRequest(
		http.MethodPost,
		"/v1/servers/sync",
		token,
		map[string]any{
			"server_id":        "server-1",
			"name":             "Stable Server",
			"text_channel_id":  "text-1",
			"voice_channel_id": "voice-1",
		})
	firstResponse := httptest.NewRecorder()
	d.handleServerSync(firstResponse, first)
	if firstResponse.Code != http.StatusCreated {
		t.Fatalf("initial sync: %d %s",
			firstResponse.Code, firstResponse.Body.String())
	}

	send := authenticatedRequest(
		http.MethodPost,
		"/v1/messages",
		token,
		map[string]any{
			"server_id":  "server-1",
			"channel_id": "text-1",
			"content":    "history must stay addressable",
		})
	sendResponse := httptest.NewRecorder()
	d.handleMessages(sendResponse, send)
	if sendResponse.Code != http.StatusCreated {
		t.Fatalf("send: %d %s",
			sendResponse.Code, sendResponse.Body.String())
	}

	change := authenticatedRequest(
		http.MethodPost,
		"/v1/servers/sync",
		token,
		map[string]any{
			"server_id":        "server-1",
			"name":             "Stable Server",
			"text_channel_id":  "text-2",
			"voice_channel_id": "voice-1",
		})
	changeResponse := httptest.NewRecorder()
	d.handleServerSync(changeResponse, change)
	if changeResponse.Code != http.StatusConflict {
		t.Fatalf("text channel identity change status = %d body %s",
			changeResponse.Code, changeResponse.Body.String())
	}

	d.mu.Lock()
	defer d.mu.Unlock()
	if d.state.Servers["server-1"].TextChannelID != "text-1" {
		t.Fatal("rejected sync changed text channel identity")
	}
	if len(d.state.Messages[messageKey("server-1", "text-1")]) != 1 {
		t.Fatal("rejected sync orphaned retained history")
	}
}


func TestDirectoryMemberRosterIsAuthorizedAndDeterministic(t *testing.T) {
	secret := []byte(strings.Repeat("r", 32))
	d, err := openDirectory(
		filepath.Join(t.TempDir(), "directory.json"),
		secret)
	if err != nil {
		t.Fatal(err)
	}

	ownerToken := registerTestUserNamed(
		t, d, "owner-1", "Zeta Owner", testCredential(0xa1))
	alphaToken := registerTestUserNamed(
		t, d, "member-a", "alice", testCredential(0xa2))
	bravoToken := registerTestUserNamed(
		t, d, "member-b", "Bravo", testCredential(0xa3))
	outsiderToken := registerTestUserNamed(
		t, d, "outsider-1", "Outsider", testCredential(0xa4))

	serverRequest := authenticatedRequest(
		http.MethodPost,
		"/v1/servers/sync",
		ownerToken,
		map[string]any{
			"server_id":        "server-1",
			"name":             "Roster Server",
			"text_channel_id":  "text-1",
			"voice_channel_id": "voice-1",
		})
	serverResponse := httptest.NewRecorder()
	d.handleServerSync(serverResponse, serverRequest)
	if serverResponse.Code != http.StatusCreated {
		t.Fatalf("sync server: %d %s",
			serverResponse.Code, serverResponse.Body.String())
	}

	d.mu.Lock()
	server := d.state.Servers["server-1"]
	server.Members["member-b"] = directoryMember{
		UserID: "member-b", Role: "member"}
	server.Members["member-a"] = directoryMember{
		UserID: "member-a", Role: "member"}
	d.state.Servers["server-1"] = server
	d.mu.Unlock()

	for name, token := range map[string]string{
		"owner": ownerToken,
		"member": alphaToken,
	} {
		t.Run(name, func(t *testing.T) {
			request := authenticatedRequest(
				http.MethodGet,
				"/v1/members?server_id=server-1",
				token,
				nil)
			response := httptest.NewRecorder()
			d.handleMembers(response, request)
			if response.Code != http.StatusOK {
				t.Fatalf("roster: %d %s",
					response.Code, response.Body.String())
			}
			var page struct {
				Members []memberDescriptor `json:"members"`
			}
			if err := json.Unmarshal(
				response.Body.Bytes(), &page); err != nil {
				t.Fatal(err)
			}
			if len(page.Members) != 3 {
				t.Fatalf("member count = %d, want 3",
					len(page.Members))
			}
			want := []string{"owner-1", "member-a", "member-b"}
			for index, member := range page.Members {
				if member.UserID != want[index] {
					t.Fatalf("member[%d] = %q, want %q",
						index, member.UserID, want[index])
				}
			}
			if page.Members[0].Role != "owner" ||
				page.Members[1].Role != "member" ||
				page.Members[2].Role != "member" {
				t.Fatalf("unexpected member roles: %#v", page.Members)
			}
			if strings.Contains(response.Body.String(), "token_hash") ||
				strings.Contains(response.Body.String(), "credential") {
				t.Fatal("roster response leaked credential material")
			}
		})
	}

	outsider := authenticatedRequest(
		http.MethodGet,
		"/v1/members?server_id=server-1",
		outsiderToken,
		nil)
	outsiderResponse := httptest.NewRecorder()
	d.handleMembers(outsiderResponse, outsider)
	if outsiderResponse.Code != http.StatusForbidden {
		t.Fatalf("outsider roster status = %d",
			outsiderResponse.Code)
	}

	_ = bravoToken
}

func TestOpenDirectoryRejectsMalformedMembershipGraph(t *testing.T) {
	secret := []byte(strings.Repeat("g", 32))

	t.Run("unknown member", func(t *testing.T) {
		path := filepath.Join(t.TempDir(), "directory.json")
		d, err := openDirectory(path, secret)
		if err != nil {
			t.Fatal(err)
		}
		token := registerTestUser(
			t, d, "owner-1", testCredential(0xb1))
		request := authenticatedRequest(
			http.MethodPost,
			"/v1/servers/sync",
			token,
			map[string]any{
				"server_id":        "server-1",
				"name":             "Broken Server",
				"text_channel_id":  "text-1",
				"voice_channel_id": "voice-1",
			})
		response := httptest.NewRecorder()
		d.handleServerSync(response, request)
		if response.Code != http.StatusCreated {
			t.Fatalf("sync: %d %s",
				response.Code, response.Body.String())
		}

		d.mu.Lock()
		server := d.state.Servers["server-1"]
		server.Members["ghost"] = directoryMember{
			UserID: "ghost", Role: "member"}
		d.state.Servers["server-1"] = server
		if err := d.persistLocked(); err != nil {
			d.mu.Unlock()
			t.Fatal(err)
		}
		d.mu.Unlock()

		if _, err := openDirectory(path, secret); err == nil {
			t.Fatal("unknown persisted member must be rejected")
		}
	})

	t.Run("second owner", func(t *testing.T) {
		path := filepath.Join(t.TempDir(), "directory.json")
		d, err := openDirectory(path, secret)
		if err != nil {
			t.Fatal(err)
		}
		token := registerTestUser(
			t, d, "owner-1", testCredential(0xb2))
		_ = registerTestUser(
			t, d, "member-1", testCredential(0xb3))
		request := authenticatedRequest(
			http.MethodPost,
			"/v1/servers/sync",
			token,
			map[string]any{
				"server_id":        "server-1",
				"name":             "Broken Owners",
				"text_channel_id":  "text-1",
				"voice_channel_id": "voice-1",
			})
		response := httptest.NewRecorder()
		d.handleServerSync(response, request)
		if response.Code != http.StatusCreated {
			t.Fatalf("sync: %d %s",
				response.Code, response.Body.String())
		}

		d.mu.Lock()
		server := d.state.Servers["server-1"]
		server.Members["member-1"] = directoryMember{
			UserID: "member-1", Role: "owner"}
		d.state.Servers["server-1"] = server
		if err := d.persistLocked(); err != nil {
			d.mu.Unlock()
			t.Fatal(err)
		}
		d.mu.Unlock()

		if _, err := openDirectory(path, secret); err == nil {
			t.Fatal("multiple persisted owners must be rejected")
		}
	})
}


func TestServerCodeLookupAndJoinRequestLifecycle(t *testing.T) {
	secret := []byte(strings.Repeat("j", 32))
	path := filepath.Join(t.TempDir(), "directory.json")
	d, err := openDirectory(path, secret)
	if err != nil {
		t.Fatal(err)
	}

	ownerToken := registerTestUserNamed(
		t, d, "owner-1", "Owner", testCredential(0xc1))
	applicantToken := registerTestUserNamed(
		t, d, "applicant-1", "Applicant", testCredential(0xc2))
	secondToken := registerTestUserNamed(
		t, d, "applicant-2", "Second", testCredential(0xc3))

	sync := authenticatedRequest(
		http.MethodPost,
		"/v1/servers/sync",
		ownerToken,
		map[string]any{
			"server_id":        "server-1",
			"name":             "Request Server",
			"text_channel_id":  "text-1",
			"voice_channel_id": "voice-1",
		})
	syncResponse := httptest.NewRecorder()
	d.handleServerSync(syncResponse, sync)
	if syncResponse.Code != http.StatusCreated {
		t.Fatalf("sync: %d %s", syncResponse.Code, syncResponse.Body.String())
	}
	var ownerServer serverDescriptor
	if err := json.Unmarshal(syncResponse.Body.Bytes(), &ownerServer); err != nil {
		t.Fatal(err)
	}
	if _, ok := canonicalServerCode(ownerServer.PublicCode); !ok {
		t.Fatalf("invalid generated Server Code %q", ownerServer.PublicCode)
	}

	lookup := authenticatedRequest(
		http.MethodGet,
		"/v1/server-lookup?code="+strings.ToLower(ownerServer.PublicCode),
		applicantToken,
		nil)
	lookupResponse := httptest.NewRecorder()
	d.handleServerLookup(lookupResponse, lookup)
	if lookupResponse.Code != http.StatusOK {
		t.Fatalf("lookup: %d %s", lookupResponse.Code, lookupResponse.Body.String())
	}
	var preview serverLookupDescriptor
	if err := json.Unmarshal(lookupResponse.Body.Bytes(), &preview); err != nil {
		t.Fatal(err)
	}
	if preview.PublicCode != ownerServer.PublicCode ||
		preview.Name != "Request Server" ||
		preview.MemberCount != 1 ||
		preview.Relationship != "none" {
		t.Fatalf("unexpected preview: %#v", preview)
	}
	for _, forbidden := range []string{
		"server-1", "owner-1", "text-1", "voice-1", "token_hash",
	} {
		if strings.Contains(lookupResponse.Body.String(), forbidden) {
			t.Fatalf("lookup leaked %q: %s", forbidden, lookupResponse.Body.String())
		}
	}

	create := authenticatedRequest(
		http.MethodPost,
		"/v1/join-requests",
		applicantToken,
		map[string]any{
			"server_code": ownerServer.PublicCode,
			"message":     "I know the owner",
		})
	createResponse := httptest.NewRecorder()
	d.handleJoinRequests(createResponse, create)
	if createResponse.Code != http.StatusCreated {
		t.Fatalf("create request: %d %s",
			createResponse.Code, createResponse.Body.String())
	}
	var request joinRequestDescriptor
	if err := json.Unmarshal(createResponse.Body.Bytes(), &request); err != nil {
		t.Fatal(err)
	}
	if request.Status != "pending" ||
		request.RequesterDisplayName != "Applicant" ||
		request.Message != "I know the owner" {
		t.Fatalf("unexpected request: %#v", request)
	}

	duplicate := authenticatedRequest(
		http.MethodPost,
		"/v1/join-requests",
		applicantToken,
		map[string]any{
			"server_code": ownerServer.PublicCode,
			"message":     "ignored duplicate",
		})
	duplicateResponse := httptest.NewRecorder()
	d.handleJoinRequests(duplicateResponse, duplicate)
	if duplicateResponse.Code != http.StatusOK {
		t.Fatalf("duplicate request: %d %s",
			duplicateResponse.Code, duplicateResponse.Body.String())
	}
	var duplicateRequest joinRequestDescriptor
	if err := json.Unmarshal(
		duplicateResponse.Body.Bytes(), &duplicateRequest); err != nil {
		t.Fatal(err)
	}
	if duplicateRequest.ID != request.ID {
		t.Fatal("duplicate create produced a second pending request")
	}

	ownerList := authenticatedRequest(
		http.MethodGet,
		"/v1/join-requests?server_id=server-1",
		ownerToken,
		nil)
	ownerListResponse := httptest.NewRecorder()
	d.handleJoinRequests(ownerListResponse, ownerList)
	if ownerListResponse.Code != http.StatusOK {
		t.Fatalf("owner list: %d %s",
			ownerListResponse.Code, ownerListResponse.Body.String())
	}
	var pendingPage struct {
		Requests []joinRequestDescriptor `json:"requests"`
	}
	if err := json.Unmarshal(
		ownerListResponse.Body.Bytes(), &pendingPage); err != nil {
		t.Fatal(err)
	}
	if len(pendingPage.Requests) != 1 ||
		pendingPage.Requests[0].RequesterDisplayName != "Applicant" {
		t.Fatalf("unexpected owner queue: %#v", pendingPage.Requests)
	}

	forbiddenDecision := authenticatedRequest(
		http.MethodPost,
		"/v1/join-requests/decision",
		secondToken,
		map[string]any{
			"request_id": request.ID,
			"decision":   "approve",
		})
	forbiddenResponse := httptest.NewRecorder()
	d.handleJoinRequestDecision(forbiddenResponse, forbiddenDecision)
	if forbiddenResponse.Code != http.StatusForbidden {
		t.Fatalf("non-owner decision status = %d",
			forbiddenResponse.Code)
	}

	approve := authenticatedRequest(
		http.MethodPost,
		"/v1/join-requests/decision",
		ownerToken,
		map[string]any{
			"request_id": request.ID,
			"decision":   "approve",
		})
	approveResponse := httptest.NewRecorder()
	d.handleJoinRequestDecision(approveResponse, approve)
	if approveResponse.Code != http.StatusOK {
		t.Fatalf("approve: %d %s",
			approveResponse.Code, approveResponse.Body.String())
	}

	d.mu.Lock()
	approvedServer := d.state.Servers["server-1"]
	approvedRequest := d.state.JoinRequests[request.ID]
	d.mu.Unlock()
	if approvedRequest.Status != "approved" {
		t.Fatalf("request status = %q", approvedRequest.Status)
	}
	if member, exists := approvedServer.Members["applicant-1"];
		!exists || member.Role != "member" {
		t.Fatal("approval did not create member")
	}

	memberLookup := authenticatedRequest(
		http.MethodGet,
		"/v1/server-lookup?code="+ownerServer.PublicCode,
		applicantToken,
		nil)
	memberLookupResponse := httptest.NewRecorder()
	d.handleServerLookup(memberLookupResponse, memberLookup)
	var memberPreview serverLookupDescriptor
	if err := json.Unmarshal(
		memberLookupResponse.Body.Bytes(), &memberPreview); err != nil {
		t.Fatal(err)
	}
	if memberPreview.Relationship != "member" {
		t.Fatalf("approved relationship = %q", memberPreview.Relationship)
	}

	rejectCreate := authenticatedRequest(
		http.MethodPost,
		"/v1/join-requests",
		secondToken,
		map[string]any{"server_code": ownerServer.PublicCode})
	rejectCreateResponse := httptest.NewRecorder()
	d.handleJoinRequests(rejectCreateResponse, rejectCreate)
	if rejectCreateResponse.Code != http.StatusCreated {
		t.Fatalf("create second request: %d %s",
			rejectCreateResponse.Code, rejectCreateResponse.Body.String())
	}
	var rejected joinRequestDescriptor
	if err := json.Unmarshal(
		rejectCreateResponse.Body.Bytes(), &rejected); err != nil {
		t.Fatal(err)
	}

	reject := authenticatedRequest(
		http.MethodPost,
		"/v1/join-requests/decision",
		ownerToken,
		map[string]any{
			"request_id": rejected.ID,
			"decision":   "reject",
		})
	rejectResponse := httptest.NewRecorder()
	d.handleJoinRequestDecision(rejectResponse, reject)
	if rejectResponse.Code != http.StatusOK {
		t.Fatalf("reject: %d %s",
			rejectResponse.Code, rejectResponse.Body.String())
	}

	reapply := authenticatedRequest(
		http.MethodPost,
		"/v1/join-requests",
		secondToken,
		map[string]any{"server_code": ownerServer.PublicCode})
	reapplyResponse := httptest.NewRecorder()
	d.handleJoinRequests(reapplyResponse, reapply)
	if reapplyResponse.Code != http.StatusConflict {
		t.Fatalf("reapply during cooldown status = %d body %s",
			reapplyResponse.Code, reapplyResponse.Body.String())
	}
}

func TestServerCodeMigrationPersistsForExistingServer(t *testing.T) {
	secret := []byte(strings.Repeat("k", 32))
	path := filepath.Join(t.TempDir(), "directory.json")
	d, err := openDirectory(path, secret)
	if err != nil {
		t.Fatal(err)
	}
	token := registerTestUser(t, d, "owner-1", testCredential(0xd1))
	sync := authenticatedRequest(
		http.MethodPost,
		"/v1/servers/sync",
		token,
		map[string]any{
			"server_id":        "server-1",
			"name":             "Migrated Server",
			"text_channel_id":  "text-1",
			"voice_channel_id": "voice-1",
		})
	response := httptest.NewRecorder()
	d.handleServerSync(response, sync)
	if response.Code != http.StatusCreated {
		t.Fatalf("sync: %d %s", response.Code, response.Body.String())
	}

	d.mu.Lock()
	server := d.state.Servers["server-1"]
	server.PublicCode = ""
	d.state.Servers["server-1"] = server
	if err := d.persistLocked(); err != nil {
		d.mu.Unlock()
		t.Fatal(err)
	}
	d.mu.Unlock()

	reloaded, err := openDirectory(path, secret)
	if err != nil {
		t.Fatal(err)
	}
	reloaded.mu.Lock()
	code := reloaded.state.Servers["server-1"].PublicCode
	reloaded.mu.Unlock()
	if _, ok := canonicalServerCode(code); !ok {
		t.Fatalf("migration generated invalid Server Code %q", code)
	}

	again, err := openDirectory(path, secret)
	if err != nil {
		t.Fatal(err)
	}
	again.mu.Lock()
	persisted := again.state.Servers["server-1"].PublicCode
	again.mu.Unlock()
	if persisted != code {
		t.Fatalf("Server Code migration was not persisted: %q -> %q",
			code, persisted)
	}
}


func TestJoinRequestCancelKeepsMembershipPrivate(t *testing.T) {
	secret := []byte(strings.Repeat("c", 32))
	d, err := openDirectory(
		filepath.Join(t.TempDir(), "directory.json"),
		secret)
	if err != nil {
		t.Fatal(err)
	}

	ownerToken := registerTestUserNamed(
		t, d, "owner-1", "Owner", testCredential(0xe1))
	applicantToken := registerTestUserNamed(
		t, d, "applicant-1", "Applicant", testCredential(0xe2))
	outsiderToken := registerTestUserNamed(
		t, d, "outsider-1", "Outsider", testCredential(0xe3))

	sync := authenticatedRequest(
		http.MethodPost,
		"/v1/servers/sync",
		ownerToken,
		map[string]any{
			"server_id":        "server-1",
			"name":             "Privacy Server",
			"text_channel_id":  "text-1",
			"voice_channel_id": "voice-1",
		})
	syncResponse := httptest.NewRecorder()
	d.handleServerSync(syncResponse, sync)
	if syncResponse.Code != http.StatusCreated {
		t.Fatalf("sync: %d %s",
			syncResponse.Code, syncResponse.Body.String())
	}
	var server serverDescriptor
	if err := json.Unmarshal(
		syncResponse.Body.Bytes(), &server); err != nil {
		t.Fatal(err)
	}

	create := authenticatedRequest(
		http.MethodPost,
		"/v1/join-requests",
		applicantToken,
		map[string]any{
			"server_code": server.PublicCode,
			"message":     "privacy test",
		})
	createResponse := httptest.NewRecorder()
	d.handleJoinRequests(createResponse, create)
	if createResponse.Code != http.StatusCreated {
		t.Fatalf("create: %d %s",
			createResponse.Code, createResponse.Body.String())
	}
	var request joinRequestDescriptor
	if err := json.Unmarshal(
		createResponse.Body.Bytes(), &request); err != nil {
		t.Fatal(err)
	}

	ownerList := authenticatedRequest(
		http.MethodGet,
		"/v1/join-requests?server_id=server-1",
		ownerToken,
		nil)
	ownerListResponse := httptest.NewRecorder()
	d.handleJoinRequests(ownerListResponse, ownerList)
	if ownerListResponse.Code != http.StatusOK {
		t.Fatalf("owner list: %d %s",
			ownerListResponse.Code, ownerListResponse.Body.String())
	}
	for _, forbidden := range []string{
		`"server_id"`,
		`"requester_id"`,
		`"owner_id"`,
		`"text_channel_id"`,
		`"voice_channel_id"`,
	} {
		if strings.Contains(ownerListResponse.Body.String(), forbidden) {
			t.Fatalf("owner request queue leaked %q: %s",
				forbidden, ownerListResponse.Body.String())
		}
	}

	outsiderCancel := authenticatedRequest(
		http.MethodPost,
		"/v1/join-requests/cancel",
		outsiderToken,
		map[string]any{"request_id": request.ID})
	outsiderCancelResponse := httptest.NewRecorder()
	d.handleJoinRequestCancel(
		outsiderCancelResponse, outsiderCancel)
	if outsiderCancelResponse.Code != http.StatusNotFound {
		t.Fatalf("outsider cancel status = %d body %s",
			outsiderCancelResponse.Code,
			outsiderCancelResponse.Body.String())
	}

	cancel := authenticatedRequest(
		http.MethodPost,
		"/v1/join-requests/cancel",
		applicantToken,
		map[string]any{"request_id": request.ID})
	cancelResponse := httptest.NewRecorder()
	d.handleJoinRequestCancel(cancelResponse, cancel)
	if cancelResponse.Code != http.StatusOK {
		t.Fatalf("cancel: %d %s",
			cancelResponse.Code, cancelResponse.Body.String())
	}
	var cancelled joinRequestDescriptor
	if err := json.Unmarshal(
		cancelResponse.Body.Bytes(), &cancelled); err != nil {
		t.Fatal(err)
	}
	if cancelled.Status != "cancelled" {
		t.Fatalf("cancelled status = %q", cancelled.Status)
	}

	d.mu.Lock()
	_, becameMember :=
		d.state.Servers["server-1"].Members["applicant-1"]
	d.mu.Unlock()
	if becameMember {
		t.Fatal("cancelled join request created membership")
	}

	lookup := authenticatedRequest(
		http.MethodGet,
		"/v1/server-lookup?code="+server.PublicCode,
		applicantToken,
		nil)
	lookupResponse := httptest.NewRecorder()
	d.handleServerLookup(lookupResponse, lookup)
	if lookupResponse.Code != http.StatusOK {
		t.Fatalf("lookup after cancel: %d %s",
			lookupResponse.Code, lookupResponse.Body.String())
	}
	var preview serverLookupDescriptor
	if err := json.Unmarshal(
		lookupResponse.Body.Bytes(), &preview); err != nil {
		t.Fatal(err)
	}
	if preview.Relationship != "none" ||
		preview.RequestID != "" {
		t.Fatalf("cancelled lookup relationship = %#v", preview)
	}
}

func TestDirectInviteResolvesPendingJoinRequest(t *testing.T) {
	secret := []byte(strings.Repeat("v", 32))
	d, err := openDirectory(
		filepath.Join(t.TempDir(), "directory.json"),
		secret)
	if err != nil {
		t.Fatal(err)
	}
	ownerToken := registerTestUserNamed(
		t, d, "owner-1", "Owner", testCredential(0xf1))
	applicantToken := registerTestUserNamed(
		t, d, "applicant-1", "Applicant", testCredential(0xf2))

	sync := authenticatedRequest(
		http.MethodPost,
		"/v1/servers/sync",
		ownerToken,
		map[string]any{
			"server_id":        "server-1",
			"name":             "Invite Reconcile",
			"text_channel_id":  "text-1",
			"voice_channel_id": "voice-1",
		})
	syncResponse := httptest.NewRecorder()
	d.handleServerSync(syncResponse, sync)
	if syncResponse.Code != http.StatusCreated {
		t.Fatalf("sync: %d %s",
			syncResponse.Code, syncResponse.Body.String())
	}
	var server serverDescriptor
	if err := json.Unmarshal(
		syncResponse.Body.Bytes(), &server); err != nil {
		t.Fatal(err)
	}

	create := authenticatedRequest(
		http.MethodPost,
		"/v1/join-requests",
		applicantToken,
		map[string]any{"server_code": server.PublicCode})
	createResponse := httptest.NewRecorder()
	d.handleJoinRequests(createResponse, create)
	if createResponse.Code != http.StatusCreated {
		t.Fatalf("create request: %d %s",
			createResponse.Code, createResponse.Body.String())
	}
	var request joinRequestDescriptor
	if err := json.Unmarshal(
		createResponse.Body.Bytes(), &request); err != nil {
		t.Fatal(err)
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
			inviteResponse.Code, inviteResponse.Body.String())
	}
	var invite struct {
		Code string `json:"code"`
	}
	if err := json.Unmarshal(
		inviteResponse.Body.Bytes(), &invite); err != nil {
		t.Fatal(err)
	}

	accept := authenticatedRequest(
		http.MethodPost,
		"/v1/invites/accept",
		applicantToken,
		map[string]any{"code": invite.Code})
	acceptResponse := httptest.NewRecorder()
	d.handleInviteAccept(acceptResponse, accept)
	if acceptResponse.Code != http.StatusOK {
		t.Fatalf("accept invite: %d %s",
			acceptResponse.Code, acceptResponse.Body.String())
	}

	d.mu.Lock()
	joined, isMember :=
		d.state.Servers["server-1"].Members["applicant-1"]
	resolved := d.state.JoinRequests[request.ID]
	d.mu.Unlock()
	if !isMember || joined.Role != "member" {
		t.Fatal("direct invite did not create membership")
	}
	if resolved.Status != "approved" {
		t.Fatalf("pending request status after invite = %q",
			resolved.Status)
	}
}

func TestOpenDirectoryRejectsApprovedJoinRequestWithoutMembership(t *testing.T) {
	secret := []byte(strings.Repeat("q", 32))
	path := filepath.Join(t.TempDir(), "directory.json")
	d, err := openDirectory(path, secret)
	if err != nil {
		t.Fatal(err)
	}
	ownerToken := registerTestUserNamed(
		t, d, "owner-1", "Owner", testCredential(0xa5))
	_ = registerTestUserNamed(
		t, d, "applicant-1", "Applicant", testCredential(0xa6))

	sync := authenticatedRequest(
		http.MethodPost,
		"/v1/servers/sync",
		ownerToken,
		map[string]any{
			"server_id":        "server-1",
			"name":             "Corrupt Request Server",
			"text_channel_id":  "text-1",
			"voice_channel_id": "voice-1",
		})
	syncResponse := httptest.NewRecorder()
	d.handleServerSync(syncResponse, sync)
	if syncResponse.Code != http.StatusCreated {
		t.Fatalf("sync: %d %s",
			syncResponse.Code, syncResponse.Body.String())
	}

	now := time.Now().Unix()
	d.mu.Lock()
	d.state.JoinRequests["request-1"] =
		directoryJoinRequest{
			ID:          "request-1",
			ServerID:    "server-1",
			RequesterID: "applicant-1",
			Status:      "approved",
			CreatedAt:   now,
			UpdatedAt:   now,
			ExpiresAt:   now + int64(resolvedJoinRequestTTL/time.Second),
		}
	if err := d.persistLocked(); err != nil {
		d.mu.Unlock()
		t.Fatal(err)
	}
	d.mu.Unlock()

	if _, err := openDirectory(path, secret); err == nil {
		t.Fatal("approved request without membership must be rejected")
	}
}
