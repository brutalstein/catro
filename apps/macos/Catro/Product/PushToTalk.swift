import AppKit
import ApplicationServices

// Saved voice preferences, shared by Settings and the voice session. Defaults match Discord:
// voice activity with automatic sensitivity and every processing stage on.
enum VoicePreferenceKey {
    static let pushToTalk = "voice.pushToTalk"
    static let pushToTalkKey = "voice.pushToTalkKey"
    static let pushToTalkName = "voice.pushToTalkName"
    static let automaticSensitivity = "voice.automaticSensitivity"
    static let sensitivityDb = "voice.sensitivityDb"
    static let echoCancellation = "voice.echoCancellation"
    static let noiseSuppression = "voice.noiseSuppression"
    static let automaticGain = "voice.automaticGain"
    static let sounds = "voice.sounds"
    // Per-member volume in percent, keyed by user id, kept across calls like Discord.
    static let memberVolumes = "voice.memberVolumes"
    // Audio endpoint ids; empty follows the system default.
    static let inputDevice = "audio.input"
    static let outputDevice = "audio.output"

    static func registerDefaults() {
        UserDefaults.standard.register(defaults: [
            pushToTalk: false, pushToTalkKey: PushToTalkShortcut.none, pushToTalkName: "",
            automaticSensitivity: true, sensitivityDb: -50.0,
            echoCancellation: true, noiseSuppression: true, automaticGain: true,
            sounds: true, inputDevice: "", outputDevice: "",
        ])
    }
}

// A key code, or a mouse button as mouseBase + button number, so side buttons work like on Windows.
enum PushToTalkShortcut {
    static let none = -1
    static let mouseBase = 1000

    // Modifier keys only report flagsChanged; their flag says whether they are down.
    private static let modifierFlags: [Int: NSEvent.ModifierFlags] = [
        54: .command, 55: .command, 56: .shift, 60: .shift, 58: .option, 61: .option,
        59: .control, 62: .control, 63: .function,
    ]

    // The shortcut an event names and whether it is now down, or nil for unrelated events.
    static func read(_ event: NSEvent) -> (shortcut: Int, down: Bool)? {
        switch event.type {
        case .keyDown: return (Int(event.keyCode), true)
        case .keyUp: return (Int(event.keyCode), false)
        case .otherMouseDown: return (mouseBase + event.buttonNumber, true)
        case .otherMouseUp: return (mouseBase + event.buttonNumber, false)
        case .flagsChanged:
            guard let flag = modifierFlags[Int(event.keyCode)] else { return nil }
            return (Int(event.keyCode), event.modifierFlags.contains(flag))
        default: return nil
        }
    }

    static func name(for event: NSEvent) -> String {
        if event.type == .otherMouseDown { return "Mouse \(event.buttonNumber + 1)" }
        switch Int(event.keyCode) {
        case 49: return "Space"
        case 36: return "Return"
        case 48: return "Tab"
        case 56, 60: return "Shift"
        case 58, 61: return "Option"
        case 59, 62: return "Control"
        case 55, 54: return "Command"
        case 63: return "Fn"
        default:
            let characters = event.charactersIgnoringModifiers?.uppercased() ?? ""
            return characters.isEmpty || event.type == .flagsChanged ? "Key \(event.keyCode)" : characters
        }
    }

    static let events: NSEvent.EventTypeMask = [.keyDown, .keyUp, .flagsChanged, .otherMouseDown, .otherMouseUp]
}

// Holds the microphone open only while the shortcut is down, in Catro or any other app. Watching
// other apps needs Accessibility access; without it the shortcut still works while Catro is active.
@MainActor
final class PushToTalkMonitor {
    private var monitors: [Any] = []
    private var down = false
    private let transmit: (Bool) -> Void

    init(transmit: @escaping (Bool) -> Void) {
        self.transmit = transmit
    }

    var running: Bool { !monitors.isEmpty }

    func start(shortcut: Int) {
        stop()
        _ = AXIsProcessTrustedWithOptions([kAXTrustedCheckOptionPrompt.takeUnretainedValue(): true] as CFDictionary)
        down = false
        transmit(false)
        let handle: (NSEvent) -> Void = { [weak self] event in
            guard let read = PushToTalkShortcut.read(event), read.shortcut == shortcut else { return }
            Task { @MainActor in self?.update(read.down) }
        }
        if let global = NSEvent.addGlobalMonitorForEvents(matching: PushToTalkShortcut.events, handler: handle) {
            monitors.append(global)
        }
        if let local = NSEvent.addLocalMonitorForEvents(matching: PushToTalkShortcut.events, handler: { event in
            handle(event)
            return event
        }) {
            monitors.append(local)
        }
    }

    func stop() {
        guard !monitors.isEmpty else { return }
        monitors.forEach(NSEvent.removeMonitor)
        monitors = []
        down = false
        transmit(true)
    }

    private func update(_ pressed: Bool) {
        guard pressed != down else { return }
        down = pressed
        transmit(pressed)
    }
}
