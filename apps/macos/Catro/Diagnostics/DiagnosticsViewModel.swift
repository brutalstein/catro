import AppKit
import SwiftUI
import UniformTypeIdentifiers

// Holds the latest diagnostics published by the bridge. Presentation comes from the shared C++
// view model, so Swift only arranges it; nothing here interprets capability evidence.
@MainActor
final class DiagnosticsViewModel: ObservableObject {
    struct Notice: Identifiable {
        enum Kind {
            case information
            case success
            case failure
        }

        let id = UUID()
        let kind: Kind
        let title: String
        let message: String
    }

    @Published private(set) var diagnostics: CatroDiagnostics?
    @Published private(set) var status = "Collecting passive capability evidence…"
    @Published var notice: Notice?

    private let bridge: CatroCapabilitiesBridge
    private var started = false

    init() {
        let directory = Bundle.main.executableURL?.deletingLastPathComponent() ?? Bundle.main.bundleURL
        bridge = CatroCapabilitiesBridge(probeURL: directory.appendingPathComponent("catro-capability-probe"))
    }

    func start() {
        guard !started else { return }
        started = true
        // The bridge delivers on the main queue; the task hop states that to the compiler.
        bridge.start { [weak self] diagnostics in
            Task { @MainActor in self?.apply(diagnostics) }
        }
    }

    func stop() {
        bridge.stop()
        started = false
    }

    func refresh() {
        guard started else { return }
        bridge.refresh()
        status = "Refreshing passive capability evidence…"
    }

    func copyReport() {
        guard let text = bridge.exportReport(with: .text) else { return }
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(text, forType: .string)
        notice = Notice(kind: .success, title: "Report copied",
                        message: "Device names are redacted; identifiers remain, so the report is not anonymous.")
    }

    // NSSavePanel confirms replacing an existing file; the atomic write never leaves it partial.
    func save(_ format: CatroExportFormat) {
        guard let text = bridge.exportReport(with: format) else { return }
        let json = format == .JSON
        let panel = NSSavePanel()
        panel.allowedContentTypes = [json ? .json : .plainText]
        panel.nameFieldStringValue = json ? "catro-capabilities.json" : "catro-capabilities.txt"
        guard panel.runModal() == .OK, let url = panel.url else { return }
        do {
            try text.write(to: url, atomically: true, encoding: .utf8)
            notice = Notice(kind: .success, title: "Report saved", message: url.path)
        } catch {
            notice = Notice(kind: .failure, title: "Export failed", message: error.localizedDescription)
        }
    }

    private func apply(_ diagnostics: CatroDiagnostics) {
        self.diagnostics = diagnostics
        let attention = diagnostics.probes.filter { $0.tone != .positive }.count
        var line = "\(diagnostics.headline) · generation \(diagnostics.generation) · \(diagnostics.probes.count) probes"
        if attention > 0 {
            line += ", \(attention) need attention"
        }
        status = line
        guard diagnostics.generation > 1, !diagnostics.changes.isEmpty else { return }
        var message = diagnostics.changes.prefix(6).joined(separator: ", ")
        if diagnostics.changes.count > 6 {
            message += ", +\(diagnostics.changes.count - 6) more"
        }
        notice = Notice(kind: .information, title: "Capabilities changed", message: message)
    }
}
