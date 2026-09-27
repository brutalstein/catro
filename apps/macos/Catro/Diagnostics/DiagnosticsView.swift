import SwiftUI

// Diagnostics workspace: a sidebar of report sections and a detail list of facts. Every value
// short of known carries a visible badge so it is never mistaken for measured evidence.
struct DiagnosticsView: View {
    @ObservedObject var model: DiagnosticsViewModel
    @State private var selection: String? = "overview"

    private static let symbols: [String: String] = [
        "overview": "square.grid.2x2", "plan": "list.bullet.rectangle", "fallbacks": "arrow.triangle.branch",
        "decisions": "checkmark.seal", "profile": "slider.horizontal.3", "probes": "stethoscope",
        "devices": "display", "system": "cpu", "runtime": "bolt", "snapshot": "camera.viewfinder",
    ]

    var body: some View {
        NavigationSplitView {
            List(selection: $selection) {
                Label("Overview", systemImage: "square.grid.2x2").tag("overview")
                ForEach(model.diagnostics?.sections ?? [], id: \.identifier) { section in
                    Label(section.title, systemImage: Self.symbols[section.identifier] ?? "doc.text")
                        .tag(section.identifier)
                }
            }
            .navigationSplitViewColumnWidth(min: 180, ideal: 210)
        } detail: {
            VStack(spacing: 0) {
                if let notice = model.notice {
                    NoticeBanner(notice: notice) { model.notice = nil }
                }
                detail
                Divider()
                Text(model.status)
                    .font(.callout)
                    .foregroundStyle(.secondary)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(.horizontal, 16)
                    .padding(.vertical, 8)
            }
        }
        .toolbar {
            ToolbarItemGroup {
                Button { model.refresh() } label: { Label("Refresh", systemImage: "arrow.clockwise") }
                    .help("Refresh passive capability evidence")
                Menu {
                    Button("Copy Report") { model.copyReport() }
                    Button("Save Text Report…") { model.save(.text) }
                    Button("Save JSON Report…") { model.save(.JSON) }
                } label: {
                    Label("Export", systemImage: "square.and.arrow.up")
                }
                .disabled(model.diagnostics == nil)
            }
        }
        .onAppear { model.start() }
        .onDisappear { model.stop() }
    }

    @ViewBuilder
    private var detail: some View {
        if let diagnostics = model.diagnostics {
            if selection == nil || selection == "overview" {
                OverviewView(diagnostics: diagnostics)
            } else if let section = diagnostics.sections.first(where: { $0.identifier == selection }) {
                SectionView(section: section)
            } else {
                Spacer()
            }
        } else {
            ProgressView("Collecting passive capability evidence…")
                .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
    }
}

private struct OverviewView: View {
    let diagnostics: CatroDiagnostics

    var body: some View {
        List {
            VStack(alignment: .leading, spacing: 8) {
                HStack(spacing: 12) {
                    Text(diagnostics.headline).font(.title2.weight(.semibold))
                    Badge(tone: diagnostics.tone, text: diagnostics.tone.name)
                }
                Text(diagnostics.detail).textSelection(.enabled)
            }
            .padding(.vertical, 6)
            .accessibilityElement(children: .combine)

            Section("Probe health") {
                ForEach(diagnostics.probes, id: \.probeID) { probe in
                    HStack(spacing: 16) {
                        Text(probe.probeID).frame(width: 240, alignment: .leading)
                        Badge(tone: probe.tone, text: probe.outcome).frame(width: 140, alignment: .leading)
                        Text("\(probe.duration) · \(probe.facts) facts").foregroundStyle(.secondary)
                        Spacer(minLength: 0)
                    }
                    .accessibilityElement(children: .combine)
                }
            }

            Section("Latest refresh") {
                if diagnostics.generation <= 1 {
                    Text("First publication; no earlier generation to compare.").foregroundStyle(.secondary)
                } else if diagnostics.changes.isEmpty {
                    Text("No capability changes since the previous generation.").foregroundStyle(.secondary)
                }
                ForEach(diagnostics.changes, id: \.self) { change in
                    Text(change).textSelection(.enabled)
                }
            }
        }
        .navigationTitle("Overview")
    }
}

private struct SectionView: View {
    let section: CatroDiagnosticsSection

    var body: some View {
        List {
            ForEach(Array(section.rows.enumerated()), id: \.offset) { _, row in
                FactRow(row: row)
            }
            if section.omittedRows > 0 {
                Text("\(section.omittedRows) more rows are in the exported report.").foregroundStyle(.secondary)
            }
        }
        .navigationTitle(section.title)
    }
}

// Label, value, and an optional badge; the whole row is one accessible element.
private struct FactRow: View {
    let row: CatroDiagnosticsRow

    var body: some View {
        let indent = CGFloat(row.depth) * 20
        let label = row.listItem ? "• \(row.label)" : row.label
        if row.value.isEmpty {
            Text(label)
                .font(.headline)
                .padding(.leading, indent)
                .padding(.top, 6)
                .accessibilityAddTraits(.isHeader)
        } else {
            HStack(alignment: .firstTextBaseline, spacing: 16) {
                Text(label)
                    .foregroundStyle(.secondary)
                    .frame(width: max(120, 260 - indent), alignment: .leading)
                Text(row.value)
                    .textSelection(.enabled)
                    .frame(maxWidth: .infinity, alignment: .leading)
                if let marker = row.state.marker {
                    Badge(tone: marker.tone, text: marker.text)
                }
            }
            .padding(.leading, indent)
            .accessibilityElement(children: .combine)
        }
    }
}

private struct Badge: View {
    let tone: CatroTone
    let text: String

    var body: some View {
        Text(text)
            .font(.caption.weight(.medium))
            .padding(.horizontal, 8)
            .padding(.vertical, 2)
            .foregroundStyle(tone.color)
            .background(tone.color.opacity(0.14), in: Capsule())
    }
}

private struct NoticeBanner: View {
    let notice: DiagnosticsViewModel.Notice
    let dismiss: () -> Void

    var body: some View {
        HStack(alignment: .top, spacing: 10) {
            Image(systemName: symbol).foregroundStyle(color)
            VStack(alignment: .leading, spacing: 2) {
                Text(notice.title).font(.headline)
                Text(notice.message).font(.callout).textSelection(.enabled)
            }
            Spacer(minLength: 0)
            Button(action: dismiss) { Image(systemName: "xmark") }
                .buttonStyle(.borderless)
                .accessibilityLabel("Dismiss")
        }
        .padding(12)
        .background(color.opacity(0.1))
        .accessibilityElement(children: .contain)
    }

    private var symbol: String {
        switch notice.kind {
        case .information: return "info.circle.fill"
        case .success: return "checkmark.circle.fill"
        case .failure: return "exclamationmark.triangle.fill"
        }
    }

    private var color: Color {
        switch notice.kind {
        case .information: return .accentColor
        case .success: return .green
        case .failure: return .red
        }
    }
}

private extension CatroTone {
    var color: Color {
        switch self {
        case .positive: return .green
        case .caution: return .orange
        case .critical: return .red
        default: return .secondary
        }
    }

    var name: String {
        switch self {
        case .positive: return "healthy"
        case .caution: return "attention"
        case .critical: return "problem"
        default: return "information"
        }
    }
}

private extension CatroFactState {
    // Known facts carry no badge; anything short of known is marked.
    var marker: (tone: CatroTone, text: String)? {
        switch self {
        case .degraded: return (.caution, "degraded")
        case .unknown: return (.neutral, "unknown")
        case .unavailable: return (.neutral, "unavailable")
        default: return nil
        }
    }
}
