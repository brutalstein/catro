import SwiftUI

// Catro's own source picker (never the stock system picker): displays and windows from
// ScreenCaptureKit and cameras, enumerated on the session worker, plus Discord's stream quality
// presets.
struct SourcePicker: View {
    @ObservedObject var model: AppModel
    @Binding var isPresented: Bool
    @State private var selection: UInt64?
    @State private var settings = ShareSettings()

    // Running games first, like Discord's "Stream <game>".
    private var sources: [CatroShareSource] {
        let all = model.snapshot?.sources ?? []
        return all.filter { $0.game } + all.filter { !$0.game }
    }
    private var firstGame: UInt64? { sources.first { $0.game }?.nativeID }
    private var selectedSource: CatroShareSource? { sources.first { $0.nativeID == selection } }
    private var sharesDisplay: Bool { selectedSource.map { !$0.window && !$0.camera } ?? false }
    private var sharesCamera: Bool { selectedSource?.camera ?? false }
    // Only cameras listed: Screen Recording is off, so say how to get displays and windows.
    private var screensBlocked: Bool { !sources.isEmpty && sources.allSatisfy { $0.camera } }

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
            .overlay(alignment: .bottom) {
                if screensBlocked {
                    Text("Allow Screen Recording in System Settings to share displays and windows.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .padding(8)
                }
            }
            .overlay {
                if sources.isEmpty {
                    Text(emptyText)
                        .foregroundStyle(.secondary)
                        .multilineTextAlignment(.center)
                        .padding()
                }
            }
            Form {
                Toggle("Show my preview", isOn: $model.localPreviewEnabled)
                    .help("Preview only changes what you see here. You can turn it on or off while sharing.")
                Picker("Resolution", selection: $settings.resolution) {
                    ForEach(ShareSettings.Resolution.allCases) { Text($0.rawValue).tag($0) }
                }
                .pickerStyle(.segmented)
                Picker("Frame rate", selection: $settings.fps) {
                    ForEach(ShareSettings.FrameRate.allCases) { Text("\($0.rawValue) FPS").tag($0) }
                }
                .pickerStyle(.segmented)
                if let source = selectedSource {
                    LabeledRow("Bitrate",
                                   value: String(format: "%.1f Mbps", settings.bitrateMbps(for: source)))
                        .help("Set from resolution and frame rate; the hardware encoder holds it")
                }
                if screenAudioCaptureAvailable {
                    Toggle(sharesDisplay ? "Share computer audio" : "Share app audio", isOn: $settings.audio)
                        .help(sharesDisplay
                            ? "Everything this Mac plays except Catro, including notification sounds"
                            : "Only the sound of the app you share")
                        .disabled(selection == nil || sharesCamera)
                } else {
                    Text("Sharing sound needs macOS 13 or later; this Mac shares the picture only.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
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
        // A detected game is picked for you; choosing anything else keeps your choice.
        .onChange(of: firstGame) { game in
            if selection == nil, let game {
                selection = game
            }
        }
        // Like Discord: an app share includes its sound by default; whole-screen audio is opt-in
        // because it also carries notifications.
        .onChange(of: selection) { _ in
            settings.audio = screenAudioCaptureAvailable && (selectedSource?.window ?? false)
            // Games move fast; 60 FPS keeps motion smooth, and the bitrate follows.
            if selectedSource?.game == true {
                settings.fps = .fps60
            }
        }
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
        let kind = source.camera ? "Camera" : source.game ? "Game" : source.window ? "Window" : "Display"
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
            Image(systemName: source.camera ? "video" : source.game ? "gamecontroller"
                : source.window ? "macwindow" : "display")
        }
        .accessibilityElement(children: .combine)
    }
}
