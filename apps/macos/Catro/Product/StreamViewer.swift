import AppKit
import SwiftUI

// The room's screen stream (or this client's local preview while sharing). The runtime renders
// into a layer this view hosts; attaching happens in NSView lifecycle, never in a view body.
struct StreamViewer: View {
    @ObservedObject var model: AppModel

    var body: some View {
        let snapshot = model.snapshot
        let sharing = snapshot?.sharing ?? false
        let watching = snapshot?.watching ?? false
        Group {
            if sharing {
                LayerHost { model.attachPreview($0) }
                    .accessibilityLabel("Your screen share preview")
            } else if snapshot?.remoteAvailable ?? false {
                VStack(spacing: 6) {
                    if watching {
                        LayerHost { model.attachRemote($0) }
                            .accessibilityLabel("Shared screen")
                    }
                    Button(watching ? "Stop Watching" : "Watch Stream") { model.setWatching(!watching) }
                        .help(watching ? "Stop decoding the shared screen" : "Decode and show the shared screen")
                }
            }
        }
        .frame(maxWidth: .infinity, maxHeight: sharing || watching ? 360 : nil)
        .padding(.horizontal, 12)
    }
}

private struct LayerHost: NSViewRepresentable {
    let attach: @MainActor (CALayer?) -> String?

    func makeNSView(context: Context) -> NSView {
        let view = NSView()
        view.wantsLayer = true
        view.layer?.backgroundColor = NSColor.black.cgColor
        _ = attach(view.layer)
        return view
    }

    func updateNSView(_ view: NSView, context: Context) {}

    static func dismantleNSView(_ view: NSView, coordinator: Coordinator) {
        _ = coordinator.attach(nil)
    }

    func makeCoordinator() -> Coordinator { Coordinator(attach: attach) }

    @MainActor
    final class Coordinator {
        let attach: @MainActor (CALayer?) -> String?
        init(attach: @escaping @MainActor (CALayer?) -> String?) { self.attach = attach }
    }
}
