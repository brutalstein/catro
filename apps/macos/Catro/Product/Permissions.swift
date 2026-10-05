import AppKit
import CoreGraphics
import Foundation

// Screen Recording and microphone access. macOS lists Catro under Privacy only after Catro asks,
// and applies a Screen Recording grant only to a fresh launch.
enum Permissions {
    enum Pane: String {
        case screenRecording = "Privacy_ScreenCapture"
        case microphone = "Privacy_Microphone"

        var title: String { self == .screenRecording ? "Screen Recording" : "Microphone" }
    }

    static var screenRecordingAllowed: Bool { CGPreflightScreenCaptureAccess() }

    // Shows the system prompt the first time and adds Catro to the Screen Recording list. Main thread.
    static func requestScreenRecording() { _ = CGRequestScreenCaptureAccess() }

    // Monterey calls it System Preferences › Security & Privacy; Ventura and later System Settings ›
    // Privacy & Security.
    static func location(of pane: Pane) -> String {
        if ProcessInfo.processInfo.isOperatingSystemAtLeast(
            OperatingSystemVersion(majorVersion: 13, minorVersion: 0, patchVersion: 0)) {
            return "System Settings › Privacy & Security › \(pane.title)"
        }
        return "System Preferences › Security & Privacy › Privacy › \(pane.title)"
    }

    static func openSettings(_ pane: Pane) {
        if let url = URL(string: "x-apple.systempreferences:com.apple.preference.security?\(pane.rawValue)") {
            NSWorkspace.shared.open(url)
        }
    }

    // Quits and opens Catro again once this process has exited, so a new grant takes effect.
    static func relaunch() {
        let reopen = Process()
        reopen.executableURL = URL(fileURLWithPath: "/bin/sh")
        reopen.arguments = ["-c", "while kill -0 \"$1\" 2>/dev/null; do sleep 0.2; done; open \"$2\"", "sh",
                            String(ProcessInfo.processInfo.processIdentifier), Bundle.main.bundlePath]
        guard (try? reopen.run()) != nil else { return }
        NSApp.terminate(nil)
    }
}
