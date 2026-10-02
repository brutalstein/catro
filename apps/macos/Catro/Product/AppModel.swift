import AppKit
import Foundation

// Owns the product bridge for the main window. Snapshots arrive on the main queue; the model only
// swaps them in. Polling ticks once per second, matching the Windows message/member cadence.
@MainActor
final class AppModel: ObservableObject {
    @Published private(set) var snapshot: CatroProductSnapshot?
    // The latest one-shot notice; the view clears it when dismissed.
    @Published var notice: String?

    private let bridge = CatroProductBridge()
    private var timer: Timer?

    var connected: Bool { snapshot?.connection == .synchronized }

    var activeServer: CatroServer? {
        guard let snapshot else { return nil }
        return snapshot.servers.first { $0.identifier == snapshot.activeServerID }
    }

    var inVoice: Bool {
        guard let phase = snapshot?.voicePhase else { return false }
        return phase == .joined || phase == .joining
    }

    func start() {
        guard timer == nil else { return }
        bridge.start { [weak self] snapshot in
            Task { @MainActor in self?.apply(snapshot) }
        }
        timer = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.bridge.poll() }
        }
    }

    func stop() {
        timer?.invalidate()
        timer = nil
        bridge.stop()
    }

    private func apply(_ snapshot: CatroProductSnapshot) {
        self.snapshot = snapshot
        if !snapshot.notice.isEmpty {
            notice = snapshot.notice
        }
    }

    func select(server identifier: String) { bridge.selectServer(identifier) }
    func send(_ message: String) { bridge.sendMessage(message) }
    func createInvite() { bridge.createInvite() }
    func acceptInvite(_ code: String) { bridge.acceptInvite(code) }
    func lookup(_ code: String) { bridge.lookupServer(code) }
    func requestJoin(code: String, note: String) { bridge.requestJoin(code: code, note: note) }
    func decide(_ request: CatroJoinRequest, approve: Bool) { bridge.decideRequest(request.identifier, approve: approve) }
    func rename(_ name: String) { bridge.renameProfile(name) }

    func toggleVoice() {
        if inVoice || snapshot?.voicePhase == .failed {
            bridge.leaveVoice()
        } else {
            bridge.joinVoice()
        }
    }

    func toggleMute() { bridge.setMuted(!(snapshot?.muted ?? false)) }
    func toggleDeafen() { bridge.setDeafened(!(snapshot?.deafened ?? false)) }
    func loadSources() { bridge.loadSources() }
    func stopShare() { bridge.stopShare() }
    func setWatching(_ watching: Bool) { bridge.setWatching(watching) }

    func share(_ source: CatroShareSource, settings: ShareSettings) {
        bridge.startShare(source, maxWidth: settings.maxWidth, maxHeight: settings.maxHeight, fps: settings.fps,
                          bitrateMbps: settings.bitrateMbps, audio: settings.audio)
    }

    // Layer hosting happens from NSView lifecycle callbacks, never from a view body.
    func attachRemote(_ layer: CALayer?) -> String? { bridge.attachRemote(layer) }
    func attachPreview(_ layer: CALayer?) -> String? { bridge.attachPreview(layer) }

    func copy(_ text: String) {
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(text, forType: .string)
    }
}

// Defaults and bounds of the Windows share dialog.
struct ShareSettings {
    var maxWidth: UInt32 = 1920
    var maxHeight: UInt32 = 1080
    var fps: UInt32 = 30
    var bitrateMbps: Double = 6
    var audio = false
}
