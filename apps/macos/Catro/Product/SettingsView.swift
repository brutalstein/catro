import AppKit
import SwiftUI

// Same appearance choices as the Windows Settings page: Ivory is the light theme, Espresso the
// dark one, and System follows macOS. Audio uses the chosen devices, or follows the system default,
// and the balanced performance profile runs, as on Windows.
enum Appearance: String, CaseIterable, Identifiable {
    case system, ivory, espresso

    var id: String { rawValue }

    var title: String {
        switch self {
        case .system: return "System"
        case .ivory: return "Ivory"
        case .espresso: return "Espresso"
        }
    }

    var colorScheme: ColorScheme? {
        switch self {
        case .system: return nil
        case .ivory: return .light
        case .espresso: return .dark
        }
    }
}

// The Catro copper accent shared with the Windows palette.
extension Color {
    static let catroAccent = Color(red: 0.878, green: 0.541, blue: 0.361)
}

struct SettingsView: View {
    @ObservedObject var model: AppModel
    // Supplies the microphone and output lists.
    @ObservedObject var devices: DiagnosticsViewModel
    @AppStorage(VoicePreferenceKey.inputDevice) private var inputDevice = ""
    @AppStorage(VoicePreferenceKey.outputDevice) private var outputDevice = ""
    @AppStorage(VoicePreferenceKey.sounds) private var sounds = true
    @AppStorage("appearance") private var appearance = Appearance.system.rawValue
    @State private var name = ""
    @AppStorage(VoicePreferenceKey.pushToTalk) private var pushToTalk = false
    @AppStorage(VoicePreferenceKey.pushToTalkKey) private var pushToTalkKey = PushToTalkShortcut.none
    @AppStorage(VoicePreferenceKey.pushToTalkName) private var pushToTalkName = ""
    @AppStorage(VoicePreferenceKey.automaticSensitivity) private var automaticSensitivity = true
    @AppStorage(VoicePreferenceKey.sensitivityDb) private var sensitivityDb = -50.0
    @AppStorage(VoicePreferenceKey.echoCancellation) private var echoCancellation = true
    @AppStorage(VoicePreferenceKey.noiseSuppression) private var noiseSuppression = true
    @AppStorage(VoicePreferenceKey.automaticGain) private var automaticGain = true
    @State private var recorder: Any?

    var body: some View {
        Form {
            Section("Profile") {
                HStack {
                    TextField("Display name", text: $name)
                        .onSubmit(save)
                    Button("Save", action: save)
                        .disabled(name.trimmingCharacters(in: .whitespaces).isEmpty ||
                                  name == model.snapshot?.identityName)
                }
                Text("The name your friends see in servers and voice.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            Section("Appearance") {
                Picker("Theme", selection: $appearance) {
                    ForEach(Appearance.allCases) { Text($0.title).tag($0.rawValue) }
                }
            }
            Section("Audio") {
                DevicePicker(title: "Microphone", selection: $inputDevice, devices: devices.audioDevices(input: true))
                DevicePicker(title: "Output", selection: $outputDevice, devices: devices.audioDevices(input: false))
                Toggle("Play join, leave and mute sounds", isOn: $sounds)
            }
            .onChange(of: inputDevice) { _ in model.applyAudioDevices() }
            .onChange(of: outputDevice) { _ in model.applyAudioDevices() }
            Section("Voice") {
                Picker("Input mode", selection: $pushToTalk) {
                    Text("Voice activity").tag(false)
                    Text("Push to talk").tag(true)
                }
                .pickerStyle(.segmented)
                if pushToTalk {
                    LabeledContent("Shortcut") {
                        Button(recorder != nil ? "Press a key…" : (pushToTalkName.isEmpty ? "Record keybind" : pushToTalkName),
                               action: record)
                    }
                    Text("Works in every app once Catro has Accessibility access. Mouse side buttons work too; Esc cancels.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                Toggle("Automatically determine input sensitivity", isOn: $automaticSensitivity)
                LabeledContent("Threshold") {
                    HStack {
                        Slider(value: $sensitivityDb, in: -100...0, step: 1)
                        Text("\(Int(sensitivityDb)) dB").monospacedDigit()
                    }
                }
                .disabled(automaticSensitivity)
                InputMeter(model: model, threshold: automaticSensitivity ? -45 : sensitivityDb)
                Toggle("Echo cancellation", isOn: $echoCancellation)
                Toggle("Noise suppression", isOn: $noiseSuppression)
                Toggle("Automatic gain control", isOn: $automaticGain)
            }
            .onChange(of: pushToTalk) { _ in model.applyVoicePreferences() }
            .onChange(of: pushToTalkKey) { _ in model.applyVoicePreferences() }
            .onChange(of: automaticSensitivity) { _ in model.applyVoicePreferences() }
            .onChange(of: sensitivityDb) { _ in model.applyVoicePreferences() }
            .onChange(of: echoCancellation) { _ in model.applyVoicePreferences() }
            .onChange(of: noiseSuppression) { _ in model.applyVoicePreferences() }
            .onChange(of: automaticGain) { _ in model.applyVoicePreferences() }
            Section("Performance") {
                Toggle("Show my screen-share preview", isOn: $model.localPreviewEnabled)
                Text("Turning preview off saves presentation work. Your stream continues at the selected quality.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                LabeledContent("Video encoding", value: "Hardware H.264")
                LabeledContent("Stream bitrate", value: "Adapts to the network")
            }
        }
        .formStyle(.grouped)
        .frame(width: 420)
        .fixedSize(horizontal: false, vertical: true)
        .onAppear {
            name = model.snapshot?.identityName ?? ""
            devices.start()
        }
        .onDisappear(perform: stopRecording)
    }

    // The next key or mouse button pressed in Catro becomes the push-to-talk shortcut.
    private func record() {
        guard recorder == nil else { return }
        recorder = NSEvent.addLocalMonitorForEvents(matching: [.keyDown, .flagsChanged, .otherMouseDown]) { event in
            guard let read = PushToTalkShortcut.read(event), read.down else { return event }
            if event.type != .keyDown || event.keyCode != 53 { // 53 is Esc
                pushToTalkKey = read.shortcut
                pushToTalkName = PushToTalkShortcut.name(for: event)
            }
            stopRecording()
            return nil
        }
    }

    private func stopRecording() {
        if let recorder {
            NSEvent.removeMonitor(recorder)
        }
        recorder = nil
    }

    private func save() {
        model.rename(name)
    }
}

// The system default first, then the devices macOS reports; a saved device that is unplugged stays
// listed so the choice survives until it returns.
private struct DevicePicker: View {
    let title: String
    @Binding var selection: String
    let devices: [CatroAudioDevice]

    var body: some View {
        Picker(title, selection: $selection) {
            ForEach(devices, id: \.label) { device in
                Text(device.label).tag(device.identifier ?? "")
            }
            if !selection.isEmpty, !devices.contains(where: { $0.identifier == selection }) {
                Text("Unavailable device").tag(selection)
            }
        }
    }
}

// Live microphone level against the voice activity threshold, like Discord's sensitivity bar. Only
// moves during a call, when the voice runtime is reading the microphone.
private struct InputMeter: View {
    @ObservedObject var model: AppModel
    let threshold: Double

    var body: some View {
        TimelineView(.periodic(from: .now, by: 0.1)) { _ in
            let level = Double(model.inputLevel())
            LabeledContent("Input level") {
                ProgressView(value: min(max(level + 100, 0), 100), total: 100)
                    .tint(level >= threshold ? .green : .secondary)
                    .help(model.inVoice ? "\(Int(level)) dB" : "Join voice to test your microphone")
            }
        }
    }
}
