import AppKit
import SwiftUI

// The room's screen stream (or this client's local preview while sharing). The runtime renders
// into a layer this view hosts; attaching happens in NSView lifecycle, never in a view body.
struct StreamViewer: View {
    @ObservedObject var model: AppModel
    @Environment(\.openWindow) private var openWindow

    var body: some View {
        let snapshot = model.snapshot
        let sharing = snapshot?.sharing ?? false
        let watching = snapshot?.watching ?? false
        let visible = (sharing || watching) && !model.streamPoppedOut
        Group {
            if model.streamPoppedOut, sharing || watching {
                HStack {
                    Text("The stream is open in its own window.").foregroundStyle(.secondary)
                    Button("Bring Back") { model.streamPoppedOut = false }
                }
            } else if sharing {
                VStack(spacing: 6) {
                    StreamLayer(model: model, preview: true)
                        .accessibilityLabel("Your screen share preview")
                    windowButtons
                }
            } else if snapshot?.remoteAvailable ?? false {
                VStack(spacing: 6) {
                    if watching {
                        StreamLayer(model: model, preview: false)
                            .accessibilityLabel("Shared screen")
                    }
                    HStack {
                        Button(watching ? "Stop Watching" : "Watch Stream") { model.setWatching(!watching) }
                            .help(watching ? "Stop decoding the shared screen" : "Decode and show the shared screen")
                        if watching {
                            windowButtons
                        }
                    }
                }
            }
        }
        .frame(maxWidth: .infinity, maxHeight: visible ? 360 : nil)
        .padding(.horizontal, 12)
    }

    private var windowButtons: some View {
        HStack {
            Button("Pop Out") { popOut(fullScreen: false) }
                .help("Show the stream in its own window")
            Button("Full Screen") { popOut(fullScreen: true) }
                .help("Show the stream full screen; Esc leaves")
        }
    }

    private func popOut(fullScreen: Bool) {
        model.streamFullScreenRequested = fullScreen
        model.streamPoppedOut = true
        openWindow(id: "stream")
    }
}

// The stream's own resizable window; it supports macOS full screen and closes back into the workspace.
struct StreamWindow: View {
    @ObservedObject var model: AppModel
    @State private var window: NSWindow?

    var body: some View {
        let snapshot = model.snapshot
        Group {
            if model.streamPoppedOut, snapshot?.sharing ?? false {
                StreamLayer(model: model, preview: true)
            } else if model.streamPoppedOut, snapshot?.watching ?? false {
                StreamLayer(model: model, preview: false)
            } else {
                Text("No stream").foregroundStyle(.secondary)
            }
        }
        .frame(minWidth: 480, minHeight: 270)
        .background(Color.black)
        .background(WindowReader { window in
            self.window = window
            guard model.streamFullScreenRequested else { return }
            model.streamFullScreenRequested = false
            if !window.styleMask.contains(.fullScreen) {
                window.toggleFullScreen(nil)
            }
        })
        .onChange(of: model.streamPoppedOut) { poppedOut in
            if !poppedOut { window?.close() }
        }
        .onDisappear { model.streamPoppedOut = false }
    }
}

private struct StreamLayer: View {
    @ObservedObject var model: AppModel
    let preview: Bool

    var body: some View {
        LayerHost(
            attach: { preview ? model.attachPreview($0) : model.attachRemote($0) },
            detach: { preview ? model.detachPreview($0) : model.detachRemote($0) })
    }
}

private struct LayerHost: NSViewRepresentable {
    let attach: @MainActor (CALayer) -> String?
    let detach: @MainActor (CALayer?) -> Void

    func makeNSView(context: Context) -> NSView {
        let view = NSView()
        view.wantsLayer = true
        view.layer?.backgroundColor = NSColor.black.cgColor
        if let layer = view.layer {
            _ = attach(layer)
        }
        return view
    }

    func updateNSView(_ view: NSView, context: Context) {}

    static func dismantleNSView(_ view: NSView, coordinator: Coordinator) {
        coordinator.detach(view.layer)
    }

    func makeCoordinator() -> Coordinator { Coordinator(detach: detach) }

    @MainActor
    final class Coordinator {
        let detach: @MainActor (CALayer?) -> Void
        init(detach: @escaping @MainActor (CALayer?) -> Void) { self.detach = detach }
    }
}

// Hands the hosting NSWindow to SwiftUI once the view is in it.
private struct WindowReader: NSViewRepresentable {
    let onWindow: @MainActor (NSWindow) -> Void

    func makeNSView(context: Context) -> NSView {
        let view = WindowReaderView()
        view.onWindow = onWindow
        return view
    }

    func updateNSView(_ view: NSView, context: Context) {}

    final class WindowReaderView: NSView {
        var onWindow: (@MainActor (NSWindow) -> Void)?

        override func viewDidMoveToWindow() {
            super.viewDidMoveToWindow()
            guard let window, let onWindow else { return }
            // Full screen needs the window on screen first.
            Task { @MainActor in onWindow(window) }
        }
    }
}
