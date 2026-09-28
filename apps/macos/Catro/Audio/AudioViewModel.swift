import Foundation

// Drives one audio test session through the bridge. Statistics are polled ten times a second
// while a session runs; the poll reads atomics and never waits for the audio thread.
@MainActor
final class AudioViewModel: ObservableObject {
    enum Mode: Int, CaseIterable, Identifiable {
        case meter
        case tone
        case monitor

        var id: Int { rawValue }

        var title: String {
            switch self {
            case .meter: return "Microphone level"
            case .tone: return "Test tone"
            case .monitor: return "Live monitor"
            }
        }

        var usesInput: Bool { self != .tone }
        var usesOutput: Bool { self != .meter }

        fileprivate var bridged: CatroAudioMode {
            switch self {
            case .meter: return .meter
            case .tone: return .tone
            case .monitor: return .monitor
            }
        }
    }

    @Published var mode: Mode = .meter
    // Nil selects the system default device.
    @Published var input: String?
    @Published var output: String?
    @Published private(set) var session: CatroAudioSession
    @Published private(set) var failure: (title: String, message: String)?
    @Published private(set) var starting = false

    private let bridge = CatroAudioBridge()
    private var timer: Timer?

    init() {
        session = bridge.session()
    }

    func start() {
        guard !starting else { return }
        failure = nil
        starting = true
        bridge.start(mode: mode.bridged, input: mode.usesInput ? input : nil,
                     output: mode.usesOutput ? output : nil) { [weak self] error in
            guard let self else { return }
            starting = false
            if let error {
                failure = ("Could not start: \(error)", Self.guidance(for: error))
                refresh()
                return
            }
            timer = Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { [weak self] _ in
                Task { @MainActor in self?.refresh() }
            }
            refresh()
        }
    }

    func stop() {
        bridge.stop()
        refresh()
    }

    private func refresh() {
        session = bridge.session()
        guard !session.running else { return }
        timer?.invalidate()
        timer = nil
        if let reason = session.failure, failure == nil {
            failure = ("Session stopped: \(reason)", Self.guidance(for: reason))
        }
    }

    // Keyed by the engine's stable failure names.
    private static func guidance(for failure: String) -> String {
        switch failure {
        case "microphone access denied":
            return "Allow Catro in System Settings › Privacy & Security › Microphone, then start again."
        case "device in use":
            return "Another app has exclusive access to the device. Close it and try again."
        case "device not found":
            return "The device is not available. Pick another device or refresh."
        case "device lost":
            return "The device was removed or reconfigured. Pick a device and start again."
        case "format unsupported":
            return "The device rejected the audio format."
        default:
            return "macOS reported an audio failure."
        }
    }
}
