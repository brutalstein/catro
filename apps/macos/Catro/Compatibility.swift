import AppKit
import SwiftUI

// Catro runs on macOS 12.3 (Monterey) and later. SwiftUI's Window scene, openWindow,
// LabeledContent and grouped forms arrived in macOS 13, so this file holds the few stand-ins:
// newer systems keep the native SwiftUI look, Monterey gets the closest equivalent.

// Single-instance auxiliary windows (the stream pop-out and diagnostics), hosted by AppKit.
@MainActor
enum AppWindows {
    private static var windows: [String: NSWindow] = [:]
    private static var observers: [String: NSObjectProtocol] = [:]

    static func show<Content: View>(_ id: String, title: String, size: CGSize, content: () -> Content) {
        if let window = windows[id] {
            window.makeKeyAndOrderFront(nil)
            return
        }
        let window = NSWindow(contentViewController: NSHostingController(rootView: content()))
        window.title = title
        window.setContentSize(size)
        window.collectionBehavior.insert(.fullScreenPrimary)
        window.isReleasedWhenClosed = false
        window.center()
        windows[id] = window
        observers[id] = NotificationCenter.default.addObserver(
            forName: NSWindow.willCloseNotification, object: window, queue: .main
        ) { _ in
            Task { @MainActor in AppWindows.closed(id) }
        }
        window.makeKeyAndOrderFront(nil)
    }

    // Tears the SwiftUI content down so its onDisappear and layer detaching run, as with a scene.
    private static func closed(_ id: String) {
        if let observer = observers.removeValue(forKey: id) {
            NotificationCenter.default.removeObserver(observer)
        }
        windows.removeValue(forKey: id)?.contentViewController = nil
    }
}

// LabeledContent on macOS 13 and later; a label and trailing value row on Monterey.
struct LabeledRow<Content: View>: View {
    let label: String
    let content: Content

    init(_ label: String, @ViewBuilder content: () -> Content) {
        self.label = label
        self.content = content()
    }

    var body: some View {
        if #available(macOS 13.0, *) {
            LabeledContent(label) { content }
        } else {
            HStack {
                Text(label)
                Spacer()
                content
            }
        }
    }
}

extension LabeledRow where Content == Text {
    init(_ label: String, value: String) {
        self.init(label) { Text(value) }
    }
}

extension View {
    // The grouped form style on macOS 13 and later; Monterey keeps its default form layout.
    @ViewBuilder
    func groupedForm() -> some View {
        if #available(macOS 13.0, *) {
            formStyle(.grouped)
        } else {
            self
        }
    }
}

// ScreenCaptureKit captures app and system audio from macOS 13; Monterey shares video only.
var screenAudioCaptureAvailable: Bool {
    if #available(macOS 13.0, *) { return true }
    return false
}
