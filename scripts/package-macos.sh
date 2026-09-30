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
staging="$dist_root/Catro-macos-$asset_arch-preview"
archive="$dist_root/Catro-macos-$asset_arch-preview.zip"
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

cat >"$staging/README.txt" <<'EOF'
Catro for macOS — preview

This is a diagnostics and local audio-only preview for macOS 13.0 or newer.
It does not provide production voice communication and does not provide screen sharing.

The app is ad-hoc signed, not notarized. Gatekeeper may require you to confirm that you
trust the downloaded app. This package does not disable or bypass Gatekeeper.
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
echo "[catro] macOS preview package: $archive"
echo "[catro] SHA-256: $digest"
