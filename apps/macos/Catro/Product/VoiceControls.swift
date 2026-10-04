import SwiftUI

// Voice and stream controls for the active server's voice channel.
struct VoiceControls: View {
    @ObservedObject var model: AppModel
    @Binding var picking: Bool

    var body: some View {
        let snapshot = model.snapshot
        let joined = snapshot?.voicePhase == .joined
        let muted = snapshot?.muted ?? false
        let deafened = snapshot?.deafened ?? false
        let sharing = snapshot?.sharing ?? false
        let otherSharing = !(snapshot?.screenOwner.isEmpty ?? true) && !sharing
        let canJoin = snapshot?.canJoinVoice ?? false
        VStack(alignment: .leading, spacing: 4) {
            HStack(spacing: 8) {
                Button { model.toggleVoice() } label: {
                    Label(model.inVoice ? "Leave Voice" : "Join Voice",
                          systemImage: model.inVoice ? "phone.down" : "phone")
                }
                .disabled(!model.inVoice && !canJoin)
                .help(canJoin ? "Join this server's voice channel"
                              : (snapshot?.connectionMessage ?? "Voice needs online services"))

                Toggle(isOn: Binding(get: { muted }, set: { _ in model.toggleMute() })) {
                    Label(muted || deafened ? "Muted" : "Mute", systemImage: muted || deafened ? "mic.slash.fill" : "mic")
                }
                .toggleStyle(.button)
                .tint(.red)
                .foregroundStyle(muted || deafened ? Color.red : Color.primary)
                .disabled(!joined)

                Toggle(isOn: Binding(get: { deafened }, set: { _ in model.toggleDeafen() })) {
                    Label(deafened ? "Deafened" : "Deafen", systemImage: deafened ? "speaker.slash.fill" : "headphones")
                }
                .toggleStyle(.button)
                .tint(.red)
                .foregroundStyle(deafened ? Color.red : Color.primary)
                .disabled(!joined)

                if sharing {
                    Button { model.stopShare() } label: {
                        Label("Stop Sharing", systemImage: "rectangle.on.rectangle.slash")
                    }
                } else {
                    Button { picking = true } label: {
                        Label("Share Screen", systemImage: "rectangle.on.rectangle")
                    }
                    .disabled(!joined || otherSharing)
                    .help(otherSharing ? "Another participant is sharing" : "Choose a display or window to share")
                }

                Spacer()

                if joined {
                    Text("\((snapshot?.peerCount ?? 0) + 1) in voice")
                        .font(.callout)
                        .foregroundStyle(.secondary)
                }
            }
            .labelStyle(.titleAndIcon)
            if let status = snapshot?.voiceStatus, !status.isEmpty {
                Text(status)
                    .font(.caption)
                    .foregroundColor(snapshot?.voicePhase == .failed ? Color.red : Color.secondary)
                    .accessibilityLabel("Voice status: \(status)")
            }
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 8)
    }
}
