import AppKit
import AVFoundation
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
    // Watched stream audio in percent (0-200), saved like Discord's stream volume.
    @Published var streamVolume = UserDefaults.standard.double(forKey: VoicePreferenceKey.streamVolume) {
        didSet {
            UserDefaults.standard.set(streamVolume, forKey: VoicePreferenceKey.streamVolume)
            applyStreamVolume()
        }
    }
    // The stream shows in its own window instead of the workspace.
    @Published var streamPoppedOut = false
    @Published var localPreviewEnabled = UserDefaults.standard.bool(forKey: "localPreviewEnabled") {
        didSet {
            UserDefaults.standard.set(localPreviewEnabled, forKey: "localPreviewEnabled")
            if !localPreviewEnabled {
                _ = bridge.attachPreview(nil)
                previewLayer = nil
                previewVisible = false
                if snapshot?.sharing == true {
                    streamPoppedOut = false
                    if stage == .local { stage = nil }
                }
            }
            applyLocalPreview()
        }
    }
    // In-app full screen, like Discord: the stream fills the Catro window. nil shows the workspace.
    enum StageSource { case local, remote }
    @Published var stage: StageSource?
    // Discord-like startup screen: up until the first sign-in settles or the user goes offline.
    @Published var starting = true
    // Microphone access is denied; the workspace offers the Microphone settings.
    @Published var microphoneBlocked = false

    private let bridge = CatroProductBridge()
    private var timer: Timer?
    private var speakingTimer: Timer?
    private lazy var pushToTalk = PushToTalkMonitor { [weak self] open in self?.bridge.setTransmit(open) }
    private var pushToTalkShortcut: Int?
    // The layers the runtime renders into now; a view only detaches its own layer.
    private weak var remoteLayer: CALayer?
    private weak var previewLayer: CALayer?
    private var previewVisible = false

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
        applyLocalPreview()
        streamVolume = UserDefaults.standard.double(forKey: VoicePreferenceKey.streamVolume)
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
        if starting && snapshot.connection != .connecting {
            starting = false
        }
        if !snapshot.notice.isEmpty {
            notice = snapshot.notice
        }
        updateSpeakingTimer()
        playCues(from: previous, to: snapshot)
        if previous?.deafened != snapshot.deafened {
            applyStreamVolume()
        }
        if !snapshot.remoteAvailable && !snapshot.sharing {
            streamPoppedOut = false
        }
        // Full screen ends with the stream it shows.
        if (stage == .local && !snapshot.sharing) || (stage == .remote && !snapshot.watching) {
            stage = nil
        }
    }

    // Deafen silences watched streams too, like Discord; the saved volume comes back after.
    private func applyStreamVolume() {
        bridge.setStreamVolume(snapshot?.deafened == true ? 0 : Float(streamVolume / 100))
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
            return
        }
        // Ask for the microphone up front so the system prompt appears; a denial still joins to
        // listen and points to the Microphone settings.
        switch AVCaptureDevice.authorizationStatus(for: .audio) {
        case .notDetermined:
            AVCaptureDevice.requestAccess(for: .audio) { granted in
                Task { @MainActor [weak self] in
                    self?.microphoneBlocked = !granted
                    self?.bridge.joinVoice()
                }
            }
        case .denied, .restricted:
            microphoneBlocked = true
            bridge.joinVoice()
        default:
            bridge.joinVoice()
        }
    }

    func toggleMute() { bridge.setMuted(!(snapshot?.muted ?? false)) }
    func toggleDeafen() { bridge.setDeafened(!(snapshot?.deafened ?? false)) }
    func loadSources() { bridge.loadSources() }
    func shareQualities(for source: CatroShareSource) -> [ShareQuality] {
        bridge.shareQualities(for: source).map {
            ShareQuality(label: $0.label, maxWidth: $0.maxWidth, maxHeight: $0.maxHeight,
                         fps: $0.fps, bitrateMbps: $0.bitrateMbps,
                         detail: $0.detail, recommended: $0.recommended)
        }
    }
    func stopShare() { bridge.stopShare() }
    func setWatching(_ watching: Bool) { bridge.setWatching(watching) }

    func share(_ source: CatroShareSource, quality: ShareQuality, audio: Bool) {
        bridge.startShare(source, maxWidth: quality.maxWidth, maxHeight: quality.maxHeight, fps: quality.fps,
                          bitrateMbps: quality.bitrateMbps, audio: audio)
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
        guard localPreviewEnabled else { return nil }
        let failure = bridge.attachPreview(layer)
        if let failure {
            notice = "Preview unavailable: \(failure)"
            return failure
        }
        previewLayer = layer
        previewVisible = false
        applyLocalPreview()
        return nil
    }

    func detachPreview(_ layer: CALayer?) {
        guard layer === previewLayer else { return }
        previewLayer = nil
        previewVisible = false
        applyLocalPreview()
        _ = bridge.attachPreview(nil)
    }

    private func applyLocalPreview() {
        bridge.setLocalPreviewEnabled(localPreviewEnabled && previewLayer != nil && previewVisible)
    }

    func setPreviewVisible(_ layer: CALayer, visible: Bool) {
        guard layer === previewLayer else { return }
        previewVisible = visible
        applyLocalPreview()
    }

    func copy(_ text: String) {
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(text, forType: .string)
    }
}
