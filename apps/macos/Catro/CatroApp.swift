import SwiftUI

@main
struct CatroApp: App {
    @StateObject private var model = DiagnosticsViewModel()

    var body: some Scene {
        Window("Catro", id: "diagnostics") {
            DiagnosticsView(model: model)
                .frame(minWidth: 860, minHeight: 540)
        }
        .commands {
            CommandGroup(replacing: .newItem) {}
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
    }
}
