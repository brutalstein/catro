import SwiftUI

// Main product window: servers on the left, the active server's text channel and voice/stream in
// the middle, members and join requests on the right. Every list is lazily rendered by List.
struct ServerWorkspace: View {
    @ObservedObject var model: AppModel
    @State private var draft = ""
    @State private var picking = false

    var body: some View {
        NavigationSplitView {
            ChannelSidebar(model: model)
                .navigationSplitViewColumnWidth(min: 200, ideal: 232)
        } content: {
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
            .navigationSplitViewColumnWidth(min: 420, ideal: 640)
            .navigationTitle(model.activeServer?.name ?? "Catro")
        } detail: {
            MemberSidebar(model: model)
                .navigationSplitViewColumnWidth(min: 180, ideal: 216)
        }
        .sheet(isPresented: $picking) {
            SourcePicker(model: model, isPresented: $picking)
        }
        .onAppear { model.start() }
        .onDisappear { model.stop() }
    }

    private var messages: some View {
        ScrollViewReader { proxy in
            List(model.snapshot?.messages ?? [], id: \.sequence) { message in
                VStack(alignment: .leading, spacing: 2) {
                    HStack(alignment: .firstTextBaseline) {
                        Text(message.author).font(.headline)
                        Text(message.createdAt, style: .time)
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                    Text(message.content).textSelection(.enabled)
                }
                .accessibilityElement(children: .combine)
                .id(message.sequence)
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
