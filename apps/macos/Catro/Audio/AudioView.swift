import SwiftUI

// The audio test page: pick devices, run a meter, tone, or monitor session, and watch its
// statistics. Leaving the page stops the session, so no device stays open out of view.
struct AudioView: View {
    @ObservedObject var audio: AudioViewModel
    let inputs: [CatroAudioDevice]
    let outputs: [CatroAudioDevice]

    var body: some View {
        let running = audio.session.running
        Form {
            Section {
                Text("Devices open only while a test runs. Levels and counters come from the running session; latency is estimated from buffer sizes, not measured.")
                    .foregroundStyle(.secondary)
            }
            Section("Devices") {
                Picker("Microphone", selection: $audio.input) {
                    ForEach(Array(inputs.enumerated()), id: \.offset) { _, device in
                        Text(device.label).tag(device.identifier)
                    }
                }
                .disabled(running || !audio.mode.usesInput)
                Picker("Speakers or headphones", selection: $audio.output) {
                    ForEach(Array(outputs.enumerated()), id: \.offset) { _, device in
                        Text(device.label).tag(device.identifier)
                    }
                }
                .disabled(running || !audio.mode.usesOutput)
            }
            Section("Test") {
                Picker("Test", selection: $audio.mode) {
                    ForEach(AudioViewModel.Mode.allCases) { mode in
                        Text(mode.title).tag(mode)
                    }
                }
                .pickerStyle(.segmented)
                .disabled(running)
                if audio.mode == .monitor {
                    Label("Use headphones: live monitor plays the microphone back, and speakers can cause loud feedback.",
                          systemImage: "headphones")
                        .foregroundStyle(.orange)
                }
                HStack(spacing: 12) {
                    Button("Start") { audio.start() }
                        .keyboardShortcut(.defaultAction)
                        .disabled(running)
                    Button("Stop") { audio.stop() }
                        .disabled(!running)
                    Text(audio.session.status)
                        .fontWeight(.semibold)
                        .foregroundStyle(color(audio.session.tone))
                }
                if let failure = audio.failure {
                    Label {
                        VStack(alignment: .leading, spacing: 2) {
                            Text(failure.title).fontWeight(.semibold)
                            Text(failure.message).foregroundStyle(.secondary)
                        }
                    } icon: {
                        Image(systemName: "exclamationmark.triangle.fill").foregroundStyle(.red)
                    }
                }
            }
            Section("Levels") {
                LevelRow(title: "Input", fraction: audio.session.inputFraction, text: audio.session.inputLevel)
                LevelRow(title: "Output", fraction: audio.session.outputFraction, text: audio.session.outputLevel)
            }
            if !audio.session.rows.isEmpty {
                Section("Session") {
                    ForEach(Array(audio.session.rows.enumerated()), id: \.offset) { _, row in
                        LabeledContent(row.label) {
                            Text(row.value).textSelection(.enabled)
                        }
                    }
                }
            }
        }
        .formStyle(.grouped)
        .onDisappear { audio.stop() }
    }

    private func color(_ tone: CatroTone) -> Color {
        switch tone {
        case .positive: return .green
        case .caution: return .orange
        case .critical: return .red
        default: return .secondary
        }
    }
}

private struct LevelRow: View {
    let title: String
    let fraction: Double
    let text: String

    var body: some View {
        HStack(spacing: 12) {
            Text(title).frame(width: 60, alignment: .leading)
            ProgressView(value: fraction)
            Text(text).monospacedDigit().frame(width: 100, alignment: .trailing)
        }
        .accessibilityElement(children: .ignore)
        .accessibilityLabel("\(title) level")
        .accessibilityValue(text)
    }
}
