import SwiftUI

// Catro's own source picker (never the stock system picker): displays and windows from
// ScreenCaptureKit, enumerated on the session worker, plus the Windows share settings.
struct SourcePicker: View {
    @ObservedObject var model: AppModel
    @Binding var isPresented: Bool
    @State private var selection: UInt64?
    @State private var settings = ShareSettings()

    private var sources: [CatroShareSource] { model.snapshot?.sources ?? [] }
    private var selectedSource: CatroShareSource? { sources.first { $0.nativeID == selection } }
    private var sharesDisplay: Bool { selectedSource.map { !$0.window } ?? false }

    private var emptyText: String {
        let status = model.snapshot?.voiceStatus ?? ""
        return status.isEmpty ? "Looking for displays and windows…" : status
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("Share your screen").font(.title2)
            List(sources, id: \.nativeID, selection: $selection) { source in
                SourceRow(source: source).tag(source.nativeID)
            }
            .frame(minHeight: 220)
            .overlay {
                if sources.isEmpty {
                    Text(emptyText)
                        .foregroundStyle(.secondary)
                        .multilineTextAlignment(.center)
                        .padding()
                }
            }
            Form {
                TextField("Maximum width", value: $settings.maxWidth, format: .number)
                    .help("320 to 7680 pixels")
                TextField("Maximum height", value: $settings.maxHeight, format: .number)
                    .help("180 to 4320 pixels")
                TextField("Frame rate", value: $settings.fps, format: .number)
                    .help("1 to 120 frames per second")
                TextField("Bitrate (Mbps)", value: $settings.bitrateMbps, format: .number)
                    .help("0.128 to 50 Mbps")
                Toggle(sharesDisplay ? "Share computer audio" : "Share app audio", isOn: $settings.audio)
                    .help(sharesDisplay
                        ? "Everything this Mac plays except Catro, including notification sounds"
                        : "Only the sound of the app you share")
                    .disabled(selection == nil)
            }
            HStack {
                Button("Refresh") { model.loadSources() }
                Spacer()
                Button("Cancel", role: .cancel) { isPresented = false }
                    .keyboardShortcut(.cancelAction)
                Button("Go Live") { goLive() }
                    .keyboardShortcut(.defaultAction)
                    .disabled(selection == nil)
            }
        }
        .padding(20)
        .frame(minWidth: 480, minHeight: 520)
        .onAppear { model.loadSources() }
        // Like Discord: an app share includes its sound by default; whole-screen audio is opt-in
        // because it also carries notifications.
        .onChange(of: selection) { _ in settings.audio = selectedSource?.window ?? false }
    }

    private func goLive() {
        if let source = selectedSource {
            model.share(source, settings: settings)
        }
        isPresented = false
    }
}

private struct SourceRow: View {
    let source: CatroShareSource

    private var detail: String {
        let kind = source.window ? "Window" : "Display"
        let main = source.primary ? " · Main display" : ""
        return "\(kind) · \(source.width)×\(source.height)\(main)"
    }

    var body: some View {
        Label {
            VStack(alignment: .leading) {
                Text(source.title.isEmpty ? source.application : source.title)
                Text(detail).font(.caption).foregroundStyle(.secondary)
            }
        } icon: {
            Image(systemName: source.window ? "macwindow" : "display")
        }
        .accessibilityElement(children: .combine)
    }
}
