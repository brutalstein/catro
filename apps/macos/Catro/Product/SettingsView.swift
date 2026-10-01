import SwiftUI

// Same rows and values as the Windows Settings page: Catro follows the system appearance and the
// system default audio devices, and runs the balanced performance profile.
struct SettingsView: View {
    var body: some View {
        Form {
            Section("Appearance") {
                LabeledContent("Theme", value: "System")
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
    }
}
