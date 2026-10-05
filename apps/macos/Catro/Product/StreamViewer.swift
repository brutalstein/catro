import AppKit
import SwiftUI

// The room's screen stream (or this client's local preview while sharing). The runtime renders
// into a layer this view hosts; attaching happens in NSView lifecycle, never in a view body.
struct StreamViewer: View {
    @ObservedObject var model: AppModel
    @AppStorage("appearance") private var appearance = Appearance.system.rawValue

    var body: some View {
        let snapshot = model.snapshot
        let sharing = snapshot?.sharing ?? false
        let watching = snapshot?.watching ?? false
        let visible = ((sharing && model.localPreviewEnabled) || watching) && !model.streamPoppedOut
        Group {
            if model.streamPoppedOut, sharing || watching {
                VStack(spacing: 6) {
                    HStack {
                        shareStatus
                        Text("The stream is open in its own window.").foregroundStyle(.secondary)
                        Button("Bring Back") { model.streamPoppedOut = false }
                        if sharing {
                            Toggle("Show my preview", isOn: $model.localPreviewEnabled)
                        }
                    }
                    if watching { StreamVolume(model: model) }
                }
            } else if sharing {
                VStack(spacing: 6) {
                    if model.localPreviewEnabled {
                        StreamLayer(model: model, preview: true)
                            .accessibilityLabel("Your screen share preview")
                    } else {
                        Text("Your screen is being shared").font(.headline)
                        Text("Preview is off. Your stream continues at the selected quality.")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                    shareStatus
                    HStack {
                        Toggle("Show my preview", isOn: $model.localPreviewEnabled)
                        if model.localPreviewEnabled { windowButtons(.local) }
                    }
                }
            } else if snapshot?.remoteAvailable ?? false {
                VStack(spacing: 6) {
                    if watching {
                        StreamLayer(model: model, preview: false)
                            .accessibilityLabel("Shared screen")
                            .help("Double-click for full screen")
                            .onTapGesture(count: 2) { model.stage = .remote }
                    }
                    HStack {
                        Button(watching ? "Stop Watching" : "Watch Stream") { model.setWatching(!watching) }
                            .help(watching ? "Stop decoding the shared screen" : "Decode and show the shared screen")
                        if watching {
                            windowButtons(.remote)
                        }
                    }
                    if watching { StreamVolume(model: model) }
                }
            }
        }
        .frame(maxWidth: .infinity, maxHeight: visible ? 360 : nil)
        .padding(.horizontal, 12)
    }

    private var shareStatus: some View {
        Group {
            if let snapshot = model.snapshot, snapshot.sharing {
                HStack {
                    Text(snapshot.shareSourceTitle)
                    if snapshot.framesSent > 0 {
                        Text("Sending \(snapshot.encodedWidth)×\(snapshot.encodedHeight)")
                        if snapshot.streamAudioActive { Text("Audio") }
                    } else {
                        Text("Starting stream…")
                    }
                }
                .font(.caption)
                .foregroundStyle(.secondary)
                .accessibilityElement(children: .combine)
            }
        }
    }

    private func windowButtons(_ source: AppModel.StageSource) -> some View {
        HStack {
            Button("Pop Out") { popOut() }
                .help("Show the stream in its own window")
            Button("Full Screen") { model.stage = source }
                .help("Fill the Catro window with the stream; Esc leaves")
        }
    }

    private func popOut() {
        model.streamPoppedOut = true
        AppWindows.show("stream", title: "Stream", size: CGSize(width: 1280, height: 720)) {
            StreamWindow(model: model)
                .preferredColorScheme(Appearance(rawValue: appearance)?.colorScheme)
        }
    }
}

// In-app full screen, like Discord: the stream fills the Catro window instead of taking over the
// display. Esc, the button or a double-click goes back to the workspace.
struct StreamStage: View {
    @ObservedObject var model: AppModel
    let source: AppModel.StageSource

    var body: some View {
        ZStack(alignment: .topTrailing) {
            Color.black
            StreamLayer(model: model, preview: source == .local)
                .accessibilityLabel(source == .local ? "Your screen share preview" : "Shared screen")
                .onTapGesture(count: 2) { model.stage = nil }
            HStack {
                if source == .remote { StreamVolume(model: model) }
                Button("Exit Full Screen") { model.stage = nil }
                    .keyboardShortcut(.cancelAction)
                    .help("Back to the channel (Esc)")
            }
            .padding(8)
            .background(.bar)
            .padding(12)
        }
    }
}

// The stream's own resizable window, for watching beside another app; it closes back into the
// workspace.
struct StreamWindow: View {
    @ObservedObject var model: AppModel
    @State private var window: NSWindow?

    var body: some View {
        let snapshot = model.snapshot
        Group {
            if model.streamPoppedOut, snapshot?.sharing ?? false {
                if model.localPreviewEnabled {
                    StreamLayer(model: model, preview: true)
                }
            } else if model.streamPoppedOut, snapshot?.watching ?? false {
                StreamLayer(model: model, preview: false)
            } else {
                Text("No stream").foregroundStyle(.secondary)
            }
        }
        .frame(minWidth: 480, minHeight: 270)
        .background(Color.black)
        .overlay(alignment: .bottomTrailing) {
            if model.streamPoppedOut, snapshot?.watching ?? false {
                HStack {
                    StreamVolume(model: model)
                    Button("Bring Back") { model.streamPoppedOut = false }
                }
                .padding(8)
                .background(.bar)
                .padding(12)
            }
        }
        .background(WindowReader { window in self.window = window })
        .onChange(of: model.streamPoppedOut) { poppedOut in
            if !poppedOut { window?.close() }
        }
        .onDisappear { model.streamPoppedOut = false }
    }
}

// Discord's per-stream volume: turns the stream's sound down without touching voice.
private struct StreamVolume: View {
    @ObservedObject var model: AppModel

    var body: some View {
        HStack(spacing: 4) {
            Image(systemName: model.streamVolume == 0 ? "speaker.slash" : "speaker.wave.2")
                .accessibilityHidden(true)
            Slider(value: $model.streamVolume, in: 0...200, step: 5)
                .frame(width: 120)
                .accessibilityLabel("Stream volume")
                .accessibilityValue("\(Int(model.streamVolume)) percent")
                .help("Stream volume: \(Int(model.streamVolume))%")
            Text("\(Int(model.streamVolume))%")
                .font(.callout)
                .monospacedDigit()
                .frame(minWidth: 40, alignment: .trailing)
        }
    }
}

private struct StreamLayer: View {
    @ObservedObject var model: AppModel
    let preview: Bool

    var body: some View {
        LayerHost(
            attach: { preview ? model.attachPreview($0) : model.attachRemote($0) },
            detach: { preview ? model.detachPreview($0) : model.detachRemote($0) },
            visibilityChanged: { layer, visible in
                if preview { model.setPreviewVisible(layer, visible: visible) }
            })
    }
}

private struct LayerHost: NSViewRepresentable {
    let attach: @MainActor (CALayer) -> String?
    let detach: @MainActor (CALayer?) -> Void
    let visibilityChanged: @MainActor (CALayer, Bool) -> Void

    func makeNSView(context: Context) -> NSView {
        let view = LayerHostView()
        view.wantsLayer = true
        view.layer?.backgroundColor = NSColor.black.cgColor
        if let layer = view.layer {
            _ = attach(layer)
        }
        view.visibilityChanged = visibilityChanged
        return view
    }

    func updateNSView(_ view: NSView, context: Context) {}

    static func dismantleNSView(_ view: NSView, coordinator: Coordinator) {
        (view as? LayerHostView)?.visibilityChanged = nil
        coordinator.detach(view.layer)
    }

    func makeCoordinator() -> Coordinator { Coordinator(detach: detach) }

    @MainActor
    final class Coordinator {
        let detach: @MainActor (CALayer?) -> Void
        init(detach: @escaping @MainActor (CALayer?) -> Void) { self.detach = detach }
    }
}

// Keep preview work asleep when its actual host window is hidden or fully covered. The local
// layer can move between the workspace and pop-out; each host only updates its own layer.
private final class LayerHostView: NSView {
    var visibilityChanged: (@MainActor (CALayer, Bool) -> Void)?
    private var windowObservation: NSObjectProtocol?

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        if let windowObservation {
            NotificationCenter.default.removeObserver(windowObservation)
            self.windowObservation = nil
        }
        if let window {
            windowObservation = NotificationCenter.default.addObserver(
                forName: NSWindow.didChangeOcclusionStateNotification, object: window, queue: .main
            ) { [weak self] _ in
                Task { @MainActor in self?.reportVisibility() }
            }
        }
        reportVisibility()
    }

    private func reportVisibility() {
        guard let layer else { return }
        let visible = window.map { window in
            window.occlusionState.contains(.visible) && !window.isMiniaturized
        } ?? false
        visibilityChanged?(layer, visible)
    }

    deinit {
        if let windowObservation {
            NotificationCenter.default.removeObserver(windowObservation)
        }
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
            Task { @MainActor in onWindow(window) }
        }
    }
}
