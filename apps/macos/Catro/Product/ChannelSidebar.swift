import SwiftUI

// Server list, the active server's channels and connected voice participants.
struct ChannelSidebar: View {
    @ObservedObject var model: AppModel
    @State private var joining = false

    private var selection: Binding<String?> {
        Binding(get: { model.snapshot?.activeServerID },
                set: {
                    if let identifier = $0,
                       model.snapshot?.servers.contains(where: { $0.identifier == identifier }) == true {
                        model.select(server: identifier)
                    }
                })
    }

    var body: some View {
        List(selection: selection) {
            Section("Servers") {
                ForEach(model.snapshot?.servers ?? [], id: \.identifier) { server in
                    Label {
                        VStack(alignment: .leading) {
                            Text(server.name)
                            Text("\(server.memberCount) member\(server.memberCount == 1 ? "" : "s")")
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                    } icon: {
                        Image(systemName: server.owner ? "crown" : "person.2")
                    }
                    .tag(server.identifier)
                    .accessibilityLabel("\(server.name), \(server.memberCount) members\(server.owner ? ", owner" : "")")
                }
            }
            if let server = model.activeServer {
                Section(server.name) {
                    if !server.textChannelID.isEmpty {
                        Label("Text", systemImage: "number")
                    }
                    if server.hasVoice {
                        Button { model.toggleVoice() } label: {
                            Label("Voice", systemImage: "speaker.wave.2")
                        }
                        .buttonStyle(.plain)
                        .disabled(!model.inVoice && !(model.snapshot?.canJoinVoice ?? false))
                        .help(model.inVoice ? "Leave this voice channel" : "Join this voice channel")
                        let participants = (model.snapshot?.members ?? []).filter {
                            $0.voiceChannelID == server.voiceChannelID
                        }
                        ForEach(participants, id: \.identifier) { member in
                            MemberRow(model: model, member: member)
                                .padding(.leading, 20)
                        }
                        if participants.isEmpty {
                            Text("Nobody in voice")
                                .font(.caption)
                                .foregroundStyle(.secondary)
                                .padding(.leading, 20)
                        }
                    }
                }
            }
            if let server = model.activeServer, server.owner, model.connected {
                Section("Invite") {
                    if !server.publicCode.isEmpty {
                        CodeRow(title: "Server code", code: server.publicCode, model: model)
                    }
                    if let invite = model.snapshot?.inviteCode, !invite.isEmpty {
                        CodeRow(title: "Invite code", code: invite, model: model)
                    }
                    Button("Create Invite") { model.createInvite() }
                        .help("Create a one-time invite code for this server")
                }
            }
        }
        .safeAreaInset(edge: .bottom) {
            Button { joining = true } label: {
                Label("Join a Server…", systemImage: "plus")
                    .frame(maxWidth: .infinity)
            }
            .disabled(!(model.snapshot?.canJoinServer ?? false))
            .help(model.connected ? "Join with an invite or request access with a server code"
                                  : (model.snapshot?.connectionMessage ?? "Online services are unavailable"))
            .padding(10)
        }
        .sheet(isPresented: $joining) {
            JoinServerSheet(model: model, isPresented: $joining)
        }
    }
}

private struct CodeRow: View {
    let title: String
    let code: String
    let model: AppModel

    var body: some View {
        HStack {
            VStack(alignment: .leading) {
                Text(title).font(.caption).foregroundStyle(.secondary)
                Text(code).font(.system(.body, design: .monospaced)).textSelection(.enabled)
            }
            Spacer()
            Button { model.copy(code) } label: { Image(systemName: "doc.on.doc") }
                .buttonStyle(.borderless)
                .accessibilityLabel("Copy \(title.lowercased())")
                .help("Copy \(title.lowercased())")
        }
    }
}

private struct JoinServerSheet: View {
    @ObservedObject var model: AppModel
    @Binding var isPresented: Bool
    @State private var invite = ""
    @State private var code = ""
    @State private var note = ""

    private func trimmed(_ text: String) -> String { text.trimmingCharacters(in: .whitespacesAndNewlines) }

    var body: some View {
        Form {
            Section("Invite") {
                TextField("Invite code", text: $invite)
                Button("Accept Invite") {
                    model.acceptInvite(trimmed(invite))
                    isPresented = false
                }
                .disabled(trimmed(invite).isEmpty)
            }
            Section("Request access") {
                TextField("Server code (CAT-…)", text: $code)
                Button("Look Up") { model.lookup(trimmed(code)) }
                    .disabled(trimmed(code).isEmpty)
                if let lookup = model.snapshot?.lookup {
                    LabeledRow(lookup.name, value: "\(lookup.memberCount) members · \(lookup.relationship)")
                    TextField("Note to the owner (optional)", text: $note)
                        .help("Up to 280 bytes")
                    Button("Send Request") {
                        model.requestJoin(code: lookup.publicCode, note: note)
                        isPresented = false
                    }
                    // Members, owners and pending requesters cannot ask again.
                    .disabled(note.utf8.count > 280 || lookup.relationship != "none")
                }
            }
        }
        .groupedForm()
        .frame(minWidth: 420, minHeight: 360)
        .toolbar {
            ToolbarItem(placement: .cancellationAction) {
                Button("Close") { isPresented = false }
            }
        }
    }
}
