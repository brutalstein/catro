import SwiftUI

@main
struct CatroApp: App {
    @StateObject private var product = AppModel()
    @StateObject private var model = DiagnosticsViewModel()
    @AppStorage("appearance") private var appearance = Appearance.system.rawValue

    private var colorScheme: ColorScheme? { Appearance(rawValue: appearance)?.colorScheme }

    var body: some Scene {
        Window("Catro", id: "workspace") {
            ServerWorkspace(model: product)
                .frame(minWidth: 960, minHeight: 600)
                .tint(.catroAccent)
                .preferredColorScheme(colorScheme)
        }
        .commands {
            CommandGroup(replacing: .newItem) {}
            CommandMenu("Voice") {
                Button(product.inVoice ? "Leave Voice" : "Join Voice") { product.toggleVoice() }
                    .keyboardShortcut("j", modifiers: [.command, .shift])
                    .disabled(!product.inVoice && !(product.snapshot?.canJoinVoice ?? false))
                Button((product.snapshot?.muted ?? false) ? "Unmute" : "Mute") { product.toggleMute() }
                    .keyboardShortcut("m", modifiers: [.command, .shift])
                    .disabled(product.snapshot?.voicePhase != .joined)
                Button((product.snapshot?.deafened ?? false) ? "Undeafen" : "Deafen") { product.toggleDeafen() }
                    .keyboardShortcut("d", modifiers: [.command, .shift])
                    .disabled(product.snapshot?.voicePhase != .joined)
                Button("Stop Sharing") { product.stopShare() }
                    .disabled(!(product.snapshot?.sharing ?? false))
            }
            CommandGroup(after: .windowArrangement) {
                OpenDiagnosticsButton()
            }
        }

        Window("Catro Diagnostics", id: "diagnostics") {
            DiagnosticsView(model: model)
                .frame(minWidth: 860, minHeight: 540)
        }
        .commands {
            CommandGroup(replacing: .saveItem) {
                Button("Save Text Report…") { model.save(.text) }
                    .keyboardShortcut("s")
                    .disabled(model.diagnostics == nil)
                Button("Save JSON Report…") { model.save(.JSON) }
                    .keyboardShortcut("s", modifiers: [.command, .shift])
                    .disabled(model.diagnostics == nil)
            }
            CommandGroup(after: .pasteboard) {
                Button("Copy Report") { model.copyReport() }
                    .keyboardShortcut("c", modifiers: [.command, .shift])
                    .disabled(model.diagnostics == nil)
            }
            CommandMenu("Diagnostics") {
                Button("Refresh") { model.refresh() }
                    .keyboardShortcut("r")
            }
        }

        Settings {
            SettingsView(model: product)
                .tint(.catroAccent)
                .preferredColorScheme(colorScheme)
        }
    }
}

private struct OpenDiagnosticsButton: View {
    @Environment(\.openWindow) private var openWindow

    var body: some View {
        Button("Diagnostics") { openWindow(id: "diagnostics") }
            .keyboardShortcut("0", modifiers: [.command, .shift])
    }
}
