import AppKit
import Metal
import SwiftUI

// One resolution choice; frame rate and starting bitrate follow from it and the GPU. These are the
// Windows presets (apps/windows/Catro/SharePresets.hpp), so both sides offer the same choices.
struct ShareQuality: Hashable {
    let label: String
    let maxWidth: UInt32
    let maxHeight: UInt32
    let fps: UInt32
    let bitrateMbps: Double

    // Apple silicon and a GPU with its own memory stream 1440p60; Intel integrated graphics stop
    // at 1080p and keep 60 FPS only at 720p.
    static let strongGPU: Bool = {
        #if arch(arm64)
        return true
        #else
        guard let device = MTLCreateSystemDefaultDevice() else { return false }
        return !device.isLowPower && !device.hasUnifiedMemory
        #endif
    }()

    private static func preset(_ label: String, height: UInt32, strongGPU: Bool) -> ShareQuality {
        let fps: UInt32 = strongGPU || height <= 720 ? 60 : 30
        // About 0.06 bits per pixel of a 16:9 frame; "p" names the height, so the box is wide.
        let pixels = Double(height) * Double(height) * 16 / 9
        return ShareQuality(label: label, maxWidth: height * 32 / 9, maxHeight: height, fps: fps,
                            bitrateMbps: min(max(pixels * Double(fps) * 0.06 / 1_000_000, 2.5), 25))
    }

    // Presets never exceed the source, so nothing is upscaled. Windows pass the screen size: a
    // window can grow while it is shared.
    static func choices(width: UInt32, height: UInt32, strongGPU: Bool)
        -> (options: [ShareQuality], recommended: Int) {
        let ceiling: UInt32 = strongGPU ? 1440 : 1080
        var options = [UInt32(720), 1080, 1440].filter { $0 <= height && $0 <= ceiling }
            .map { preset("\($0)p", height: $0, strongGPU: strongGPU) }
        if height > (options.last?.maxHeight ?? 0) && (strongGPU || height <= ceiling) {
            // The floor keeps a tiny source inside what the screen runtime accepts.
            let source = preset("Source", height: min(max(height, 180), 4320), strongGPU: strongGPU)
            options.append(ShareQuality(label: source.label, maxWidth: min(max(width, 320), 7680),
                                        maxHeight: source.maxHeight, fps: source.fps,
                                        bitrateMbps: source.bitrateMbps))
        }
        if options.isEmpty {
            options = [preset("1080p", height: 1080, strongGPU: strongGPU)]
        }
        // The best the computer handles, but not a 4K source by default.
        var recommended = options.count - 1
        if recommended > 0 && options[recommended].maxHeight > 1440 {
            recommended -= 1
        }
        return (options, recommended)
    }
}

// The name a person recognizes: "Brave - YouTube", "Counter-Strike 2", "Discord".
enum ShareNames {
    private static let browsers = [("brave", "Brave"), ("chrome", "Chrome"), ("edge", "Edge"),
                                   ("firefox", "Firefox"), ("safari", "Safari"), ("opera", "Opera"),
                                   ("vivaldi", "Vivaldi")]

    static func window(title: String, application: String, game: Bool) -> String {
        let title = title.trimmingCharacters(in: .whitespacesAndNewlines)
        if game && !title.isEmpty {
            return title
        }
        let app = application.lowercased()
        if let browser = browsers.first(where: { app.contains($0.0) })?.1 {
            // "Lofi beats - YouTube" -> "YouTube"; "(3) Inbox - Gmail" -> "Gmail"
            let parts = title.replacingOccurrences(of: " \u{2013} ", with: " - ")
                .replacingOccurrences(of: " \u{2014} ", with: " - ")
                .components(separatedBy: " - ")
                .map { $0.trimmingCharacters(in: .whitespaces) }
                .filter { !$0.isEmpty && !$0.lowercased().contains(browser.lowercased()) }
            guard var site = parts.last else { return browser }
            if site.hasPrefix("("), let close = site.range(of: ") ") {
                site = String(site[close.upperBound...])
            }
            return "\(browser) - \(site)"
        }
        return application.isEmpty ? title : application
    }
}

// Catro's own source picker (never the stock system picker): displays and windows from
// ScreenCaptureKit and cameras, enumerated on the session worker. Grouped like Discord; the user
// picks only the resolution.
struct SourcePicker: View {
    @ObservedObject var model: AppModel
    @Binding var isPresented: Bool
    @State private var selection: UInt64?
    @State private var qualityIndex = 0
    @State private var audio = false
    // Without Screen Recording only cameras are listed; say how to get displays and windows.
    @State private var screensBlocked = !Permissions.screenRecordingAllowed

    private var sources: [CatroShareSource] { model.snapshot?.sources ?? [] }
    private var games: [CatroShareSource] { sources.filter { $0.game } }
    private var screens: [CatroShareSource] { sources.filter { !$0.game && !$0.window && !$0.camera } }
    private var apps: [CatroShareSource] { sources.filter { !$0.game && $0.window } }
    private var cameras: [CatroShareSource] { sources.filter { !$0.game && $0.camera } }
    private var firstGame: UInt64? { games.first?.nativeID }
    private var selectedSource: CatroShareSource? { sources.first { $0.nativeID == selection } }
    private var sharesDisplay: Bool { selectedSource.map { !$0.window && !$0.camera } ?? false }
    private var sharesCamera: Bool { selectedSource?.camera ?? false }

    private var qualities: (options: [ShareQuality], recommended: Int) {
        guard let source = selectedSource else { return ([], 0) }
        let screen = source.window ? screens.max { $0.height < $1.height } : nil
        return ShareQuality.choices(width: max(source.width, screen?.width ?? 0),
                                    height: max(source.height, screen?.height ?? 0),
                                    strongGPU: ShareQuality.strongGPU)
    }
    private var quality: ShareQuality? {
        let options = qualities.options
        return options.indices.contains(qualityIndex) ? options[qualityIndex] : nil
    }

    private var emptyText: String {
        let status = model.snapshot?.voiceStatus ?? ""
        return status.isEmpty ? "Looking for displays and windows…" : status
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("Share your screen").font(.title2)
            if screensBlocked {
                ScreenRecordingNotice()
            }
            List {
                group("Games", games)
                group("Screens", screens)
                group("Apps", apps)
                group("Cameras", cameras)
            }
            .frame(minHeight: 260)
            .overlay {
                if sources.isEmpty && !screensBlocked {
                    Text(emptyText)
                        .foregroundStyle(.secondary)
                        .multilineTextAlignment(.center)
                        .padding()
                }
            }
            Form {
                if let quality {
                    Picker("Resolution", selection: $qualityIndex) {
                        ForEach(Array(qualities.options.enumerated()), id: \.offset) { index, option in
                            Text(option.label).tag(index)
                        }
                    }
                    .pickerStyle(.segmented)
                    .help("Frame rate and quality are set for this Mac")
                    Text("\(quality.label) · \(quality.fps) FPS · "
                         + (ShareQuality.strongGPU ? "tuned for your graphics" : "tuned for integrated graphics"))
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                Toggle("Show my preview", isOn: $model.localPreviewEnabled)
                    .help("Preview only changes what you see here. You can turn it on or off while sharing.")
                if screenAudioCaptureAvailable {
                    Toggle(sharesDisplay ? "Share computer audio" : "Share app audio", isOn: $audio)
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
                Button("Refresh") {
                    screensBlocked = !Permissions.screenRecordingAllowed
                    model.loadSources()
                }
                Spacer()
                Button("Cancel", role: .cancel) { isPresented = false }
                    .keyboardShortcut(.cancelAction)
                Button("Go Live") { goLive() }
                    .keyboardShortcut(.defaultAction)
                    .disabled(selectedSource == nil || quality == nil)
            }
        }
        .padding(20)
        .frame(minWidth: 480, minHeight: 540)
        .onAppear {
            // Asking is what adds Catro to the Screen Recording list; macOS prompts only once.
            if screensBlocked { Permissions.requestScreenRecording() }
            model.loadSources()
        }
        // A detected game is picked for you; choosing anything else keeps your choice.
        .onChange(of: firstGame) { game in
            if selection == nil, let game {
                selection = game
            }
        }
        // Like Discord: an app share includes its sound by default; whole-screen audio is opt-in
        // because it also carries notifications.
        .onChange(of: selection) { _ in
            audio = screenAudioCaptureAvailable && (selectedSource?.window ?? false)
            qualityIndex = qualities.recommended
        }
    }

    // Click a source to pick it; click it again to clear the choice.
    @ViewBuilder
    private func group(_ title: String, _ items: [CatroShareSource]) -> some View {
        if !items.isEmpty {
            Section(title) {
                ForEach(items, id: \.nativeID) { source in
                    let chosen = selection == source.nativeID
                    Button {
                        selection = chosen ? nil : source.nativeID
                    } label: {
                        SourceRow(source: source, name: name(of: source), detail: detail(of: source),
                                  selected: chosen)
                    }
                    .buttonStyle(.plain)
                    .accessibilityAddTraits(chosen ? .isSelected : [])
                }
            }
        }
    }

    private func name(of source: CatroShareSource) -> String {
        if source.camera {
            return source.title.isEmpty ? "Camera" : source.title
        }
        if !source.window {
            let number = (screens.firstIndex { $0.nativeID == source.nativeID } ?? 0) + 1
            return "Screen \(number)" + (source.primary ? " (main)" : "")
        }
        return ShareNames.window(title: source.title, application: source.application, game: source.game)
    }

    private func detail(of source: CatroShareSource) -> String {
        if source.camera { return "Camera" }
        if !source.window { return "\(source.width)×\(source.height)" }
        if source.game { return "Game" }
        let title = source.title.trimmingCharacters(in: .whitespacesAndNewlines)
        return title == name(of: source) ? "" : title
    }

    private func goLive() {
        if let source = selectedSource, let quality {
            model.share(source, quality: quality, audio: audio)
        }
        isPresented = false
    }
}

private struct SourceRow: View {
    let source: CatroShareSource
    let name: String
    let detail: String
    let selected: Bool

    // The program's own icon, like Discord; screens, cameras and unknown apps get a symbol.
    private var appIcon: NSImage? {
        guard source.window, !source.application.isEmpty else { return nil }
        return NSWorkspace.shared.runningApplications.first { $0.localizedName == source.application }?.icon
    }

    var body: some View {
        HStack(spacing: 10) {
            if let appIcon {
                Image(nsImage: appIcon).resizable().frame(width: 28, height: 28)
            } else {
                Image(systemName: source.camera ? "video" : source.game ? "gamecontroller"
                    : source.window ? "macwindow" : "display")
                    .font(.title2)
                    .frame(width: 28, height: 28)
            }
            VStack(alignment: .leading, spacing: 2) {
                Text(name).fontWeight(.semibold).lineLimit(1)
                if !detail.isEmpty {
                    Text(detail).font(.caption).foregroundStyle(.secondary).lineLimit(1)
                }
            }
            Spacer(minLength: 0)
            if selected {
                Image(systemName: "checkmark.circle.fill").foregroundColor(.accentColor)
            }
        }
        .padding(.vertical, 4)
        .padding(.horizontal, 6)
        .background(RoundedRectangle(cornerRadius: 6)
            .fill(selected ? Color.accentColor.opacity(0.18) : Color.clear))
        .contentShape(Rectangle())
        .accessibilityElement(children: .combine)
    }
}

// Screen Recording is off: where to turn it on, and the restart macOS needs before it applies.
private struct ScreenRecordingNotice: View {
    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Catro needs Screen Recording permission to share displays and windows.")
                .font(.callout.weight(.semibold))
            Text("Turn on Catro in \(Permissions.location(of: .screenRecording)), then restart Catro.")
                .font(.caption)
                .foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
            HStack {
                Button("Open Settings") { Permissions.openSettings(.screenRecording) }
                    .help("Opens the Screen Recording list")
                Button("Restart Catro") { Permissions.relaunch() }
                    .help("macOS applies Screen Recording permission after Catro restarts")
            }
        }
        .padding(10)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(RoundedRectangle(cornerRadius: 8).fill(Color.secondary.opacity(0.12)))
    }
}
