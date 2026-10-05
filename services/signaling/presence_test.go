package main

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"path/filepath"
	"testing"
	"time"
)

func TestMemberVoicePresenceFollowsLiveRoom(t *testing.T) {
	s, roomServer, secret := newTestRoomService(t)
	d, err := openDirectory(filepath.Join(t.TempDir(), "directory.json"), []byte(secret))
	if err != nil {
		t.Fatal(err)
	}
	d.signaling = s
	ownerToken := registerTestUserNamed(t, d, "owner-1", "Owner", testCredential(0xd1))
	registerTestUserNamed(t, d, "member-1", "Member", testCredential(0xd2))
	registerTestUserNamed(t, d, "other-1", "Other", testCredential(0xd3))
	response := httptest.NewRecorder()
	d.handleServerSync(response, authenticatedRequest(http.MethodPost, "/v1/servers/sync", ownerToken,
		map[string]any{"server_id": "server-1", "name": "Presence", "text_channel_id": "text-1", "voice_channel_id": "voice-1"}))
	if response.Code != http.StatusCreated {
		t.Fatalf("sync: %d %s", response.Code, response.Body.String())
	}
	d.mu.Lock()
	server := d.state.Servers["server-1"]
	server.Members["member-1"] = directoryMember{UserID: "member-1", Role: "member"}
	server.Members["other-1"] = directoryMember{UserID: "other-1", Role: "member"}
	d.state.Servers[server.ID] = server
	d.mu.Unlock()

	roster := func() map[string]string {
		t.Helper()
		response := httptest.NewRecorder()
		d.handleMembers(response, authenticatedRequest(http.MethodGet, "/v1/members?server_id=server-1", ownerToken, nil))
		if response.Code != http.StatusOK {
			t.Fatalf("roster: %d %s", response.Code, response.Body.String())
		}
		var page struct {
			Members []struct {
				UserID         string `json:"user_id"`
				VoiceChannelID string `json:"voice_channel_id"`
			} `json:"members"`
		}
		if err := json.Unmarshal(response.Body.Bytes(), &page); err != nil {
			t.Fatal(err)
		}
		if len(page.Members) != 3 {
			t.Fatalf("presence changed roster membership: %#v", page.Members)
		}
		result := make(map[string]string)
		for _, member := range page.Members {
			result[member.UserID] = member.VoiceChannelID
		}
		return result
	}
	for id, channel := range roster() {
		if channel != "" {
			t.Fatalf("%s is present before joining", id)
		}
	}
	if len(s.rooms) != 0 {
		t.Fatal("roster read created an empty room")
	}
	// A peer with the same ID in another channel or server must stay absent here.
	if _, err := s.roomFor("server-1", "voice-other").add(&client{id: "other-1"}); err != nil {
		t.Fatal(err)
	}
	if _, err := s.roomFor("server-other", "voice-1").add(&client{id: "other-1"}); err != nil {
		t.Fatal(err)
	}
	joinTestRoomPeer(t, roomServer, secret, "owner-1")
	member, _ := joinTestRoomPeer(t, roomServer, secret, "member-1")
	joinTestRoomPeer(t, roomServer, secret, "not-a-member")
	present := roster()
	if present["owner-1"] != "voice-1" || present["member-1"] != "voice-1" || present["other-1"] != "" {
		t.Fatalf("joined presence = %#v", present)
	}
	if err := member.Close(); err != nil {
		t.Fatal(err)
	}
	deadline := time.Now().Add(2 * time.Second)
	for roster()["member-1"] != "" {
		if time.Now().After(deadline) {
			t.Fatal("disconnected member remained in voice")
		}
		time.Sleep(5 * time.Millisecond)
	}
	if roster()["owner-1"] != "voice-1" {
		t.Fatal("disconnect removed another peer")
	}
}
