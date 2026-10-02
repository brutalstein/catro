import SwiftUI

// Same appearance choices as the Windows Settings page: Ivory is the light theme, Espresso the
// dark one, and System follows macOS. Audio follows the system default devices and the balanced
// performance profile runs, as on Windows.
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
    @AppStorage("appearance") private var appearance = Appearance.system.rawValue
    @State private var name = ""

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
                LabeledContent("Microphone", value: "Default")
                LabeledContent("Output", value: "Default")
            }
            Section("Performance") {
                LabeledContent("Profile", value: "Balanced")
            }
        }
        .formStyle(.grouped)
        .frame(width: 420)
        .fixedSize(horizontal: false, vertical: true)
        .onAppear { name = model.snapshot?.identityName ?? "" }
    }

    private func save() {
        model.rename(name)
    }
}
