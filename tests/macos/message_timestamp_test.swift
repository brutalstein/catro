import Foundation

@main
struct MessageTimestampTest {
    static func main() {
        var calendar = Calendar(identifier: .gregorian)
        calendar.timeZone = TimeZone(identifier: "America/New_York")!
        let locale = Locale(identifier: "en_US")
        func date(_ value: String) -> Date { ISO8601DateFormatter().date(from: value)! }
        func label(_ value: String, now: String) -> String {
            MessageTimestamp.dayLabel(for: date(value), now: date(now), calendar: calendar, locale: locale)
        }
        // Local midnight and the year boundary matter, not elapsed hours or UTC dates.
        precondition(label("2026-01-01T04:59:00Z", now: "2026-01-01T05:01:00Z") == "Yesterday")
        precondition(label("2026-01-01T05:00:00Z", now: "2026-01-01T05:01:00Z") == "Today")
        precondition(label("2025-12-30T17:00:00Z", now: "2026-01-01T05:01:00Z") == "Dec 30, 2025")
        // Yesterday is a calendar day even when a DST transition makes it 23 hours long.
        precondition(label("2026-03-08T05:01:00Z", now: "2026-03-09T04:01:00Z") == "Yesterday")
        precondition(label("2026-03-09T04:00:00Z", now: "2026-03-09T04:01:00Z") == "Today")
        print("Message timestamp tests passed")
    }
}
