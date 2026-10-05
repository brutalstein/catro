import SwiftUI

// Members of the active server (owner first) and, for owners, pending join requests.
struct MemberSidebar: View {
    @ObservedObject var model: AppModel

    var body: some View {
        List {
            let requests = model.snapshot?.pendingRequests ?? []
            if !requests.isEmpty {
                Section("Join requests") {
                    ForEach(requests, id: \.identifier) { request in
                        VStack(alignment: .leading, spacing: 4) {
                            Text(request.requester).font(.headline)
                            if !request.message.isEmpty {
                                Text(request.message).font(.callout).foregroundStyle(.secondary)
                            }
                            HStack {
                                Button("Approve") { model.decide(request, approve: true) }
                                    .accessibilityLabel("Approve \(request.requester)")
                                Button("Reject") { model.decide(request, approve: false) }
                                    .accessibilityLabel("Reject \(request.requester)")
                            }
                        }
                    }
                }
            }
            Section("Members") {
                ForEach(model.snapshot?.members ?? [], id: \.identifier) { member in
                    MemberRow(model: model, member: member)
                }
            }
        }
        .overlay {
            if model.snapshot?.members.isEmpty ?? true {
                Text(model.connected ? "Loading members…" : "Members need online services")
                    .foregroundStyle(.secondary)
            }
        }
    }
}

// Shared by the member list and connected participants beneath the voice channel.
struct MemberRow: View {
    @ObservedObject var model: AppModel
    let member: CatroMember
    @State private var showVolume = false

    var body: some View {
        Group {
            if member.isSelf {
                label
            } else {
                Button { showVolume = true } label: { label }
                    .buttonStyle(.plain)
                    .help("Adjust \(member.displayName)'s voice volume")
            }
        }
        .contextMenu {
            if !member.isSelf {
                Button("User Volume…") { showVolume = true }
            }
        }
        .popover(isPresented: $showVolume) {
            VStack(alignment: .leading, spacing: 8) {
                Text("\(member.displayName)'s voice volume").font(.headline)
                Slider(value: Binding(get: { model.volume(for: member.identifier) },
                                      set: { model.setVolume($0, for: member.identifier) }),
                       in: 0...200, step: 1)
                    .accessibilityLabel("Voice volume for \(member.displayName)")
                Text("\(Int(model.volume(for: member.identifier)))%")
                    .font(.callout)
                    .foregroundStyle(.secondary)
            }
            .padding()
            .frame(width: 240)
        }
    }

    private var label: some View {
        let speaking = model.speaking.contains(member.identifier)
        return Label {
            Text(member.isSelf ? "\(member.displayName) (you)" : member.displayName)
        } icon: {
            Image(systemName: member.owner ? "crown" : "person")
                .padding(3)
                .overlay(Circle().stroke(Color.green, lineWidth: 2).opacity(speaking ? 1 : 0))
        }
        .accessibilityLabel("\(member.displayName)\(member.owner ? ", owner" : "")\(member.isSelf ? ", you" : "")\(speaking ? ", speaking" : "")")
    }
}
