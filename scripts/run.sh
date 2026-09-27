#!/bin/sh
# Opens the SwiftUI diagnostics shell, or with --report writes a capability report instead.
# Example: scripts/run.sh --report --format json --output out/report.json
set -eu
configuration=${CATRO_CONFIGURATION:-Debug}
preset=${CATRO_PRESET:-macos-clang}
output="$(cd "$(dirname "$0")/.." && pwd)/out/build/$preset/$configuration"

if [ "${1:-}" = "--report" ]; then
    shift
    tool="$output/catro-capability-report"
    [ -x "$tool" ] || { echo "Report tool not built: $tool. Run scripts/build.sh." >&2; exit 1; }
    exec "$tool" "$@"
fi
app="$output/Catro.app"
[ -d "$app" ] || { echo "Shell not built: $app. Run scripts/build.sh." >&2; exit 1; }
exec open "$app"
