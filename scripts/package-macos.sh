#!/bin/sh
set -eu

configuration=${1:-Release}
preset=${CATRO_PRESET:-macos-clang}
root=$(cd "$(dirname "$0")/.." && pwd)

case "$(uname -m)" in
    arm64)
        host_arch=arm64
        asset_arch=arm64
        ;;
    x86_64)
        host_arch=x86_64
        asset_arch=x64
        ;;
    *)
        echo "Unsupported macOS architecture: $(uname -m)" >&2
        exit 1
        ;;
esac

dist_root="$root/out/dist"
staging="$dist_root/Catro-macos-$asset_arch"
archive="$dist_root/Catro-macos-$asset_arch.zip"
# Production packages name their Catro service; without it the app runs in local mode.
service_url=${CATRO_SERVICE_URL:-}
case "$service_url" in
    '') ;;
    https://localhost*|https://127.*|https://\[::1\]*|*\?*|*\#*|*@*)
        echo "Catro service URL must be a public HTTPS origin without credentials or query: $service_url" >&2
        exit 1
        ;;
    https://*) service_url=${service_url%/} ;;
    *)
        echo "Catro service URL must use HTTPS: $service_url" >&2
        exit 1
        ;;
esac
checksum="$archive.sha256"
source_app="$root/out/build/$preset/$configuration/Catro.app"
staged_app="$staging/Catro.app"
complete=0

cleanup() {
    if [ "$complete" -ne 1 ]; then
        rm -f "$archive" "$checksum"
    fi
}
trap cleanup 0
trap 'exit 1' HUP INT TERM

"$root/scripts/build.sh" "$configuration"

[ -d "$source_app" ] || {
    echo "Catro.app not found: $source_app" >&2
    exit 1
}
[ -f "$source_app/Contents/MacOS/catro-capability-probe" ] || {
    echo "Capability probe not found in Catro.app." >&2
    exit 1
}

mkdir -p "$dist_root"
rm -rf "$staging"
mkdir -p "$staging"
ditto "$source_app" "$staged_app"

if [ -n "$service_url" ]; then
    # The shell bundle ships no other resources, so Contents/Resources may not exist yet.
    mkdir -p "$staged_app/Contents/Resources"
    printf '{\n  "api_base_url": "%s",\n  "allow_insecure_http": false\n}\n' "$service_url" \
        >"$staged_app/Contents/Resources/catro-network.json"
fi

cat >"$staging/README.txt" <<'EOF'
Catro for macOS

Move Catro.app to Applications and open it. Requires macOS 13.0 or newer.

Voice channels, screen and window sharing, and the #general text channel work with friends on
Windows and macOS. macOS asks for Microphone access when you first join voice and for Screen
Recording access when you first share. Sharing app audio from a Mac is not available yet; your
stream shares video and your voice still works.

The app is ad-hoc signed, not notarized. Gatekeeper may require you to confirm that you
trust the downloaded app: Control-click Catro.app, choose Open, then Open again. This package
does not disable or bypass Gatekeeper.
EOF

codesign --force --deep --sign - "$staged_app"
codesign --verify --deep --strict "$staged_app"

archs=$(lipo -archs "$staged_app/Contents/MacOS/Catro")
[ "$archs" = "$host_arch" ] || {
    echo "Catro executable architecture mismatch: expected $host_arch, got $archs" >&2
    exit 1
}

rm -f "$archive" "$checksum"
(cd "$staging" && ditto -c -k --sequesterRsrc . "$archive")
digest=$(shasum -a 256 "$archive" | awk '{print tolower($1)}')
printf '%s  %s\n' "$digest" "$(basename "$archive")" >"$checksum"

complete=1
echo "[catro] macOS package: $archive"
echo "[catro] SHA-256: $digest"
