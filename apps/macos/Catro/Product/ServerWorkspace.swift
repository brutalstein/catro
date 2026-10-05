import AppKit
import SwiftUI

// Main product window: servers on the left, the active server's text channel and voice/stream in
// the middle, members and join requests on the right. Every list is lazily rendered by List.
struct ServerWorkspace: View {
    @ObservedObject var model: AppModel
    @State private var draft = ""
    @State private var picking = false
    @StateObject private var updater = Updater()
    @Environment(\.accessibilityReduceMotion) private var reduceMotion

    var body: some View {
        Group {
            if updater.installing {
                StartupSplash(model: model, status: "Downloading Catro \(updateVersion)…")
                    .transition(.opacity)
            } else if model.starting {
                StartupSplash(model: model)
                    .transition(.opacity)
            } else if let stage = model.stage {
                StreamStage(model: model, source: stage)
            } else {
                columns
            }
        }
        .animation(reduceMotion ? nil : .easeOut(duration: 0.25), value: model.starting)
        .animation(reduceMotion ? nil : .easeOut(duration: 0.25), value: updater.installing)
        .toolbar {
            ToolbarItem(placement: .primaryAction) {
                if updater.available != nil && !updater.installing {
                    Button { updater.confirming = true } label: {
                        Label("Update", systemImage: "arrow.down.circle.fill")
                    }
                    .labelStyle(.titleAndIcon)
                    .tint(.green)
                    .help("Update to Catro \(updateVersion)")
                    .accessibilityLabel("Update to Catro \(updateVersion)")
                }
            }
        }
        .alert("Catro \(updateVersion) is ready", isPresented: $updater.confirming) {
            Button("Install Update") { updater.install() }
            Button("Later", role: .cancel) {}
        } message: {
            Text("Catro downloads the update, quits, and opens again on the new version in a few seconds. "
                 + "Calls and streams stop during the restart.")
        }
        // A separate view: two alerts on one view can hide each other on macOS 12.
        .background(Color.clear.alert("Update failed", isPresented: failureShown) {
            Button("OK", role: .cancel) {}
        } message: {
            Text(updater.failure ?? "")
        })
        .sheet(isPresented: $picking) {
            SourcePicker(model: model, isPresented: $picking)
        }
        .onAppear {
            model.start()
            updater.start()
        }
        .onDisappear { model.stop() }
    }

    private var updateVersion: String { ReleaseTag.display(updater.available ?? "") }

    private var failureShown: Binding<Bool> {
        Binding(get: { updater.failure != nil }, set: { if !$0 { updater.failure = nil } })
    }

    // NavigationSplitView needs macOS 13; Monterey gets the same three columns from NavigationView.
    @ViewBuilder
    private var columns: some View {
        if #available(macOS 13.0, *) {
            NavigationSplitView {
                ChannelSidebar(model: model)
                    .navigationSplitViewColumnWidth(min: 200, ideal: 232)
            } content: {
                center
                    .navigationSplitViewColumnWidth(min: 420, ideal: 640)
            } detail: {
                MemberSidebar(model: model)
                    .navigationSplitViewColumnWidth(min: 180, ideal: 216)
            }
        } else {
            NavigationView {
                ChannelSidebar(model: model)
                    .frame(minWidth: 200, idealWidth: 232)
                center
                    .frame(minWidth: 420, idealWidth: 640)
                MemberSidebar(model: model)
                    .frame(minWidth: 180, idealWidth: 216)
            }
        }
    }

    private var center: some View {
        VStack(spacing: 0) {
            if let notice = model.notice {
                NoticeBar(text: notice) { model.notice = nil }
            }
            ConnectionBar(snapshot: model.snapshot)
            VoiceControls(model: model, picking: $picking)
            if model.snapshot?.voicePhase == .joined {
                StreamViewer(model: model)
            }
            Divider()
            messages
            Divider()
            composer
        }
        .navigationTitle(model.activeServer?.name ?? "Catro")
    }

    private var messages: some View {
        let items = model.snapshot?.messages ?? []
        let calendar = Calendar.current
        let now = Date()
        return ScrollViewReader { proxy in
            List {
                ForEach(Array(items.enumerated()), id: \.element.sequence) { index, message in
                    VStack(alignment: .leading, spacing: 2) {
                        let day = MessageTimestamp.dayLabel(for: message.createdAt, now: now)
                        if index == 0 || !calendar.isDate(items[index - 1].createdAt, inSameDayAs: message.createdAt) {
                            VStack(spacing: 4) {
                                Text(day).font(.caption).foregroundStyle(.secondary)
                                Divider()
                            }
                            .padding(.vertical, 8)
                        }
                        HStack(alignment: .firstTextBaseline) {
                            Text(message.author).font(.headline)
                            Text(day).font(.caption).foregroundStyle(.secondary)
                            Text(message.createdAt, style: .time)
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                        .help(message.createdAt.formatted(date: .complete, time: .complete))
                        Text(message.content).textSelection(.enabled)
                    }
                    .accessibilityElement(children: .combine)
                    .id(message.sequence)
                }
            }
            .overlay {
                if model.snapshot?.messages.isEmpty ?? true {
                    Text(model.connected ? "No messages yet" : "Messages need online services")
                        .foregroundStyle(.secondary)
                }
            }
            .onChange(of: model.snapshot?.messages.last?.sequence) { last in
                if let last { proxy.scrollTo(last, anchor: .bottom) }
            }
        }
    }

    private var composer: some View {
        HStack {
            TextField("Message", text: $draft)
                .textFieldStyle(.roundedBorder)
                .onSubmit { send() }
                .accessibilityLabel("Message")
                .help("Messages are 1 to 2000 bytes")
            Button("Send") { send() }
                .keyboardShortcut(.return, modifiers: .command)
                .disabled(!canSend)
        }
        .padding(10)
        .disabled(!(model.snapshot?.canSendMessage ?? false))
    }

    private var canSend: Bool {
        let bytes = draft.utf8.count
        return (model.snapshot?.canSendMessage ?? false) && bytes > 0 && bytes <= 2000
    }

    private func send() {
        guard canSend else { return }
        model.send(draft)
        draft = ""
    }
}

private struct ConnectionBar: View {
    let snapshot: CatroProductSnapshot?

    var body: some View {
        if let snapshot, snapshot.connection != .synchronized {
            Label(snapshot.connectionMessage,
                  systemImage: snapshot.connection == .connecting ? "arrow.triangle.2.circlepath" : "wifi.slash")
                .font(.callout)
                .foregroundStyle(.secondary)
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding(.horizontal, 12)
                .padding(.vertical, 6)
                .background(.bar)
        }
    }
}

struct NoticeBar: View {
    let text: String
    let dismiss: () -> Void

    var body: some View {
        HStack {
            Text(text).frame(maxWidth: .infinity, alignment: .leading)
            Button(action: dismiss) { Image(systemName: "xmark") }
                .buttonStyle(.borderless)
                .accessibilityLabel("Dismiss notice")
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 6)
        .background(Color.yellow.opacity(0.15))
    }
}

// Shown at launch until the first sign-in settles, like Discord's loading screen. Sign-in keeps
// retrying behind it; after a few seconds the user may go on offline. An in-app update reuses it
// with its own status and no way out.
struct StartupSplash: View {
    @ObservedObject var model: AppModel
    var status: String? = nil
    @State private var slow = false

    private var message: String {
        if let status = status { return status }
        let text = model.snapshot?.connectionMessage ?? ""
        return text.isEmpty ? "Starting Catro…" : text
    }

    var body: some View {
        VStack(spacing: 18) {
            Image(nsImage: NSApp.applicationIconImage)
                .resizable()
                .frame(width: 96, height: 96)
                .accessibilityHidden(true)
            Text("CATRO")
                .font(.title2.weight(.semibold))
                .tracking(6)
            ProgressView()
                .controlSize(.small)
            Text(message)
                .foregroundColor(.secondary)
                .multilineTextAlignment(.center)
                .frame(maxWidth: 360)
            if slow && status == nil {
                Button("Continue Offline") { model.starting = false }
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .task {
            try? await Task.sleep(nanoseconds: 6_000_000_000)
            slow = true
        }
    }
}
