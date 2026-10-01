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
                    Label {
                        Text(member.isSelf ? "\(member.displayName) (you)" : member.displayName)
                    } icon: {
                        Image(systemName: member.owner ? "crown" : "person")
                    }
                    .accessibilityLabel("\(member.displayName)\(member.owner ? ", owner" : "")\(member.isSelf ? ", you" : "")")
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
