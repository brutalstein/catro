import Foundation

@main
struct ReleaseTagTest {
    static func main() {
        let base = "https://github.com/brutalstein/catro/releases/tag/"
        precondition(ReleaseTag.tag(fromRedirect: base + "v0.3.8") == "v0.3.8")
        precondition(ReleaseTag.tag(fromRedirect: base + "v0.3.8/") == "v0.3.8")
        // Only Catro's own release pages, and only plain tag characters.
        precondition(ReleaseTag.tag(fromRedirect: "https://github.com/other/catro/releases/tag/v9.9.9") == nil)
        precondition(ReleaseTag.tag(fromRedirect: base) == nil)
        precondition(ReleaseTag.tag(fromRedirect: base + "v1.0.0?x=1") == nil)
        // Strictly newer x.y.z releases only.
        precondition(ReleaseTag.isNewer("v0.3.8", than: "0.3.7"))
        precondition(ReleaseTag.isNewer("v0.10.0", than: "0.9.9"))
        precondition(!ReleaseTag.isNewer("v0.3.7", than: "0.3.7"))
        precondition(!ReleaseTag.isNewer("v0.3.6", than: "0.3.7"))
        precondition(!ReleaseTag.isNewer("v0.4.0-beta", than: "0.3.7"))
        precondition(!ReleaseTag.isNewer("v0.4", than: "0.3.7"))
        precondition(ReleaseTag.display("v0.3.8") == "0.3.8")
        print("Release tag tests passed")
    }
}
