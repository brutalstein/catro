#!/bin/sh
set -eu

release_base=${CATRO_RELEASE_BASE_URL:-https://github.com/brutalstein/catro/releases}
install_root=${CATRO_INSTALL_ROOT:-"$HOME/Applications/Catro.app"}
requested_version=${CATRO_VERSION:-}
architecture=${CATRO_TEST_ARCHITECTURE:-$(uname -m)}

case "$architecture" in
    arm64) asset_arch=arm64 ;;
    x86_64) asset_arch=x64 ;;
    *)
        echo "Catro for macOS supports arm64 and x86_64; detected architecture '$architecture'." >&2
        exit 1
        ;;
esac

case "$release_base" in
    https://*) ;;
    http://127.0.0.1:*|http://127.0.0.1/*|http://localhost:*|http://localhost/*|http://\[::1\]:*|http://\[::1\]/*)
        [ -n "${CATRO_RELEASE_BASE_URL:-}" ] || {
            echo 'HTTP is allowed only for an explicit loopback test URL.' >&2
            exit 1
        }
        ;;
    *)
        echo 'Catro release downloads require HTTPS; HTTP is allowed only for an explicit loopback test URL.' >&2
        exit 1
        ;;
esac

asset_name="Catro-macos-$asset_arch.zip"
checksum_name="$asset_name.sha256"
if [ -n "$requested_version" ]; then
    selector="download/$requested_version"
    installed_version=$requested_version
else
    selector=latest/download
    installed_version=latest
fi

temporary_root=$(mktemp -d "${TMPDIR:-/tmp}/catro-install.XXXXXX")
archive="$temporary_root/$asset_name"
checksum_file="$temporary_root/$checksum_name"
staging="$temporary_root/staging"
staged_app="$staging/Catro.app"
backup="$install_root.backup-$$"
backup_created=0

cleanup() {
    rm -rf "$temporary_root"
}
trap cleanup 0
trap 'exit 1' HUP INT TERM

mkdir -p "$staging"
curl -fL "$release_base/$selector/$asset_name" -o "$archive"
curl -fL "$release_base/$selector/$checksum_name" -o "$checksum_file"

expected_digest=$(
    awk '
        NR != 1 || NF < 1 { exit 1 }
        {
            digest = $1
            if (length(digest) != 64 || digest !~ /^[0-9A-Fa-f]+$/) exit 1
        }
        END {
            if (NR != 1) exit 1
            print tolower(digest)
        }
    ' "$checksum_file"
) || {
    echo 'Release checksum must contain exactly one 64-character SHA-256 digest.' >&2
    exit 1
}
actual_digest=$(shasum -a 256 "$archive" | awk '{print tolower($1)}')
[ "$expected_digest" = "$actual_digest" ] || {
    echo "Release checksum mismatch for $asset_name." >&2
    exit 1
}

ditto -x -k "$archive" "$staging"
[ -f "$staged_app/Contents/MacOS/Catro" ] && [ -x "$staged_app/Contents/MacOS/Catro" ] || {
    echo 'Release package does not contain an executable Catro.app/Contents/MacOS/Catro.' >&2
    exit 1
}

install_parent=$(dirname "$install_root")
mkdir -p "$install_parent"
[ ! -e "$backup" ] || {
    echo "Installer backup path already exists: $backup" >&2
    exit 1
}

if [ -e "$install_root" ]; then
    mv "$install_root" "$backup"
    backup_created=1
fi

replace_failed=0
if [ "$backup_created" -eq 1 ] && [ "${CATRO_TEST_FAIL_AFTER_BACKUP:-}" = 1 ]; then
    replace_failed=1
elif ! mv "$staged_app" "$install_root"; then
    replace_failed=1
fi

if [ "$replace_failed" -eq 1 ]; then
    rm -rf "$install_root"
    if [ "$backup_created" -eq 1 ]; then
        mv "$backup" "$install_root"
    fi
    echo 'Catro installation failed; the previous installation was restored.' >&2
    exit 1
fi

if [ "$backup_created" -eq 1 ]; then
    rm -rf "$backup"
fi

echo "[catro] Installed version: $installed_version"
echo "[catro] App: $install_root"
