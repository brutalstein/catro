import AppKit
import Foundation

// Owns the product bridge for the main window. Snapshots arrive on the main queue; the model only
// swaps them in. Polling ticks once per second, matching the Windows message/member cadence.
@MainActor
final class AppModel: ObservableObject {
    @Published private(set) var snapshot: CatroProductSnapshot?
    // The latest one-shot notice; the view clears it when dismissed.
    @Published var notice: String?
    // Members speaking now, refreshed four times a second while in voice.
    @Published private(set) var speaking: Set<String> = []
    // Per-member volume in percent (0-200), saved across calls like Discord.
    @Published private(set) var memberVolumes: [String: Double] =
        UserDefaults.standard.dictionary(forKey: VoicePreferenceKey.memberVolumes) as? [String: Double] ?? [:]
    // The stream shows in its own window instead of the workspace.
    @Published var streamPoppedOut = false
    // Set by "Full Screen"; the stream window consumes it once it has a window.
    var streamFullScreenRequested = false

    private let bridge = CatroProductBridge()
    private var timer: Timer?
    private var speakingTimer: Timer?
    private lazy var pushToTalk = PushToTalkMonitor { [weak self] open in self?.bridge.setTransmit(open) }
    private var pushToTalkShortcut: Int?
    // The layers the runtime renders into now; a view only detaches its own layer.
    private weak var remoteLayer: CALayer?
    private weak var previewLayer: CALayer?

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
        VoicePreferenceKey.registerDefaults()
        bridge.start { [weak self] snapshot in
            Task { @MainActor in self?.apply(snapshot) }
        }
        applyAudioDevices()
        timer = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.bridge.poll() }
        }
    }

    func stop() {
        timer?.invalidate()
        timer = nil
        speakingTimer?.invalidate()
        speakingTimer = nil
        pushToTalk.stop()
        bridge.stop()
    }

    private func apply(_ snapshot: CatroProductSnapshot) {
        let previous = self.snapshot
        self.snapshot = snapshot
        if !snapshot.notice.isEmpty {
            notice = snapshot.notice
        }
        updateSpeakingTimer()
        playCues(from: previous, to: snapshot)
        if !snapshot.remoteAvailable && !snapshot.sharing {
            streamPoppedOut = false
        }
    }

    // Discord-style cues for joining, leaving, people coming and going, mute and deafen.
    private func playCues(from old: CatroProductSnapshot?, to new: CatroProductSnapshot) {
        guard UserDefaults.standard.bool(forKey: VoicePreferenceKey.sounds) else { return }
        let wasJoined = old?.voicePhase == .joined
        let isJoined = new.voicePhase == .joined
        if wasJoined != isJoined {
            play(isJoined ? "Glass" : "Bottle")
            return
        }
        guard isJoined, let old else { return }
        if new.peerCount > old.peerCount {
            play("Pop")
        } else if new.peerCount < old.peerCount {
            play("Bottle")
        }
        if new.deafened != old.deafened {
            play(new.deafened ? "Funk" : "Purr")
        } else if new.muted != old.muted {
            play(new.muted ? "Tink" : "Morse")
        }
    }

    private func play(_ name: String) {
        NSSound(named: NSSound.Name(name))?.play()
    }

    // Speaking reads are lock-free atomics, so the timer only runs while joined.
    private func updateSpeakingTimer() {
        let joined = snapshot?.voicePhase == .joined
        if joined, speakingTimer == nil {
            speakingTimer = Timer.scheduledTimer(withTimeInterval: 0.25, repeats: true) { [weak self] _ in
                Task { @MainActor in self?.refreshSpeaking() }
            }
            applyVoicePreferences()
            for (identifier, percent) in memberVolumes {
                bridge.setVolume(Float(percent / 100), forMember: identifier)
            }
        } else if !joined, let speakingTimer {
            speakingTimer.invalidate()
            self.speakingTimer = nil
            speaking = []
            updatePushToTalk()
        }
    }

    // Sends the saved Voice settings to the call; Settings calls this after every change.
    func applyVoicePreferences() {
        let defaults = UserDefaults.standard
        bridge.setVoiceProcessing(echo: defaults.bool(forKey: VoicePreferenceKey.echoCancellation),
                                  noise: defaults.bool(forKey: VoicePreferenceKey.noiseSuppression),
                                  gain: defaults.bool(forKey: VoicePreferenceKey.automaticGain))
        bridge.setInputThreshold(defaults.bool(forKey: VoicePreferenceKey.automaticSensitivity)
            ? .nan : Float(defaults.double(forKey: VoicePreferenceKey.sensitivityDb)))
        updatePushToTalk()
    }

    // Sends the saved microphone and output to the next join, and moves a running call to them.
    func applyAudioDevices() {
        let defaults = UserDefaults.standard
        let input = defaults.string(forKey: VoicePreferenceKey.inputDevice) ?? ""
        let output = defaults.string(forKey: VoicePreferenceKey.outputDevice) ?? ""
        bridge.setAudioDevices(input: input.isEmpty ? nil : input, output: output.isEmpty ? nil : output)
    }

    // Microphone level in dBFS for the Settings meter; -100 outside voice.
    func inputLevel() -> Float { bridge.inputLevel() }

    private func updatePushToTalk() {
        let defaults = UserDefaults.standard
        let shortcut = defaults.integer(forKey: VoicePreferenceKey.pushToTalkKey)
        let active = snapshot?.voicePhase == .joined && defaults.bool(forKey: VoicePreferenceKey.pushToTalk) &&
            shortcut != PushToTalkShortcut.none
        if active, pushToTalkShortcut != shortcut {
            pushToTalk.start(shortcut: shortcut)
            pushToTalkShortcut = shortcut
        } else if !active, pushToTalkShortcut != nil {
            pushToTalk.stop()
            pushToTalkShortcut = nil
        }
    }

    private func refreshSpeaking() {
        let now = Set(bridge.speakingMembers())
        if now != speaking {
            speaking = now
        }
    }

    func volume(for identifier: String) -> Double { memberVolumes[identifier] ?? 100 }

    func setVolume(_ percent: Double, for identifier: String) {
        if percent == 100 {
            memberVolumes.removeValue(forKey: identifier)
        } else {
            memberVolumes[identifier] = percent
        }
        UserDefaults.standard.set(memberVolumes, forKey: VoicePreferenceKey.memberVolumes)
        bridge.setVolume(Float(percent / 100), forMember: identifier)
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
        let size = settings.size(for: source)
        bridge.startShare(source, maxWidth: size.width, maxHeight: size.height, fps: settings.fps.rawValue,
                          bitrateMbps: settings.bitrateMbps(for: source), audio: settings.audio)
    }

    // Layer hosting happens from NSView lifecycle callbacks, never from a view body. A view
    // detaching passes its own layer, so a stream that already moved to another window stays.
    func attachRemote(_ layer: CALayer) -> String? {
        remoteLayer = layer
        return bridge.attachRemote(layer)
    }

    func detachRemote(_ layer: CALayer?) {
        guard layer === remoteLayer else { return }
        remoteLayer = nil
        _ = bridge.attachRemote(nil)
    }

    func attachPreview(_ layer: CALayer) -> String? {
        previewLayer = layer
        return bridge.attachPreview(layer)
    }

    func detachPreview(_ layer: CALayer?) {
        guard layer === previewLayer else { return }
        previewLayer = nil
        _ = bridge.attachPreview(nil)
    }

    func copy(_ text: String) {
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(text, forType: .string)
    }
}

// Discord's stream quality choices. Bitrate follows resolution and frame rate (about 0.07 bits per
// pixel for screen content) so the encoder never starves at 60 fps or wastes bandwidth at 15.
struct ShareSettings {
    enum Resolution: String, CaseIterable, Identifiable {
        case p720 = "720p", p1080 = "1080p", p1440 = "1440p", source = "Source"
        var id: String { rawValue }
    }

    enum FrameRate: UInt32, CaseIterable, Identifiable {
        case fps15 = 15, fps30 = 30, fps60 = 60
        var id: UInt32 { rawValue }
    }

    var resolution = Resolution.p1080
    var fps = FrameRate.fps30
    var audio = false

    func size(for source: CatroShareSource) -> (width: UInt32, height: UInt32) {
        switch resolution {
        case .p720: return (1280, 720)
        case .p1080: return (1920, 1080)
        case .p1440: return (2560, 1440)
        case .source: return (min(max(source.width, 320), 7680), min(max(source.height, 180), 4320))
        }
    }

    func bitrateMbps(for source: CatroShareSource) -> Double {
        let size = size(for: source)
        // The capture keeps the source aspect inside the box, so a smaller source sends less.
        let width = Double(min(size.width, max(source.width, 1)))
        let height = Double(min(size.height, max(source.height, 1)))
        let mbps = width * height * Double(fps.rawValue) * 0.07 / 1_000_000
        return min(max(mbps, 1.5), 25)
    }
}
