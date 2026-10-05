import AppKit
import Foundation

// Release tags come from GitHub's latest-release redirect, like the Windows shell's AppUpdate.cpp.
enum ReleaseTag {
    static let latestURL = URL(string: "https://github.com/brutalstein/catro/releases/latest")!
    private static let tagPrefix = "https://github.com/brutalstein/catro/releases/tag/"
    private static let tagCharacters = CharacterSet(
        charactersIn: "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz._-")

    static func tag(fromRedirect location: String) -> String? {
        guard location.hasPrefix(tagPrefix) else { return nil }
        var tag = String(location.dropFirst(tagPrefix.count))
        if tag.hasSuffix("/") { tag.removeLast() }
        guard !tag.isEmpty, tag.unicodeScalars.allSatisfy(tagCharacters.contains) else { return nil }
        return tag
    }

    // x.y.z with an optional leading "v"; anything else is not a release Catro installs.
    static func version(_ text: String) -> [Int]? {
        let body = text.hasPrefix("v") ? text.dropFirst() : Substring(text)
        let parts = body.split(separator: ".", omittingEmptySubsequences: false)
        let numbers = parts.compactMap { part -> Int? in
            guard !part.isEmpty, part.allSatisfy({ ("0"..."9").contains($0) }) else { return nil }
            return Int(part)
        }
        return parts.count == 3 && numbers.count == 3 ? numbers : nil
    }

    static func isNewer(_ tag: String, than current: String) -> Bool {
        guard let candidate = version(tag), let installed = version(current) else { return false }
        return installed.lexicographicallyPrecedes(candidate)
    }

    static func display(_ tag: String) -> String { tag.hasPrefix("v") ? String(tag.dropFirst()) : tag }
}

// Reads the redirect instead of following it: one small request, no GitHub API quota.
private final class RedirectBlocker: NSObject, URLSessionTaskDelegate {
    func urlSession(_ session: URLSession, task: URLSessionTask, willPerformHTTPRedirection response: HTTPURLResponse,
                    newRequest request: URLRequest, completionHandler: @escaping (URLRequest?) -> Void) {
        completionHandler(nil)
    }
}

// In-app updates: checks for a newer release every few hours and installs it with the bundled
// installer, which downloads and verifies the build, waits for Catro to quit, swaps the app and
// opens it again (the old build if anything failed).
@MainActor
final class Updater: ObservableObject {
    @Published private(set) var available: String?
    @Published private(set) var installing = false
    @Published var failure: String?
    @Published var confirming = false

    private var timer: Timer?
    private let session = URLSession(configuration: .ephemeral, delegate: RedirectBlocker(), delegateQueue: nil)

    private var installer: URL? { Bundle.main.url(forResource: "install-macos", withExtension: "sh") }
    private var currentVersion: String {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? ""
    }

    func start() {
        // Development builds carry no installer and never offer updates.
        guard installer != nil, timer == nil else { return }
        timer = Timer.scheduledTimer(withTimeInterval: 6 * 60 * 60, repeats: true) { [weak self] _ in
            Task { @MainActor in await self?.check() }
        }
        Task { await check() }
    }

    func check() async {
        let response = (try? await session.data(from: ReleaseTag.latestURL))?.1
        guard !installing,
              let location = (response as? HTTPURLResponse)?.value(forHTTPHeaderField: "Location"),
              let tag = ReleaseTag.tag(fromRedirect: location),
              ReleaseTag.isNewer(tag, than: currentVersion) else { return }
        available = tag
    }

    func install() {
        guard let tag = available, let installer = installer, !installing else { return }
        installing = true
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent("catro-update")
        let script = directory.appendingPathComponent("install-macos.sh")
        let status = directory.appendingPathComponent("status")
        let process = Process()
        do {
            // The installer replaces this bundle, so it runs from a copy in the temp directory.
            try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
            try? FileManager.default.removeItem(at: script)
            try? FileManager.default.removeItem(at: status)
            try FileManager.default.copyItem(at: installer, to: script)
            process.executableURL = URL(fileURLWithPath: "/bin/sh")
            process.arguments = [script.path]
            process.currentDirectoryURL = directory
            var environment = ProcessInfo.processInfo.environment
            environment["CATRO_VERSION"] = tag
            environment["CATRO_INSTALL_ROOT"] = Bundle.main.bundlePath
            environment["CATRO_WAIT_PID"] = String(ProcessInfo.processInfo.processIdentifier)
            environment["CATRO_STATUS_FILE"] = status.path
            environment["CATRO_RELAUNCH"] = "1"
            process.environment = environment
            try process.run()
        } catch {
            fail("The installer could not be started.")
            return
        }
        Task {
            // The installer writes "ready" once the new build is verified, then waits for Catro to quit.
            while true {
                try? await Task.sleep(nanoseconds: 400_000_000)
                let state = (try? String(contentsOf: status, encoding: .utf8))?
                    .trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
                if state == "ready" {
                    NSApp.terminate(nil)
                    return
                }
                if state.hasPrefix("failed") || !process.isRunning {
                    fail("The download did not finish.")
                    return
                }
            }
        }
    }

    private func fail(_ reason: String) {
        installing = false
        failure = "Catro is unchanged. \(reason) Try again from the update button."
    }
}
