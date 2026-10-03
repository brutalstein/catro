#!/bin/sh
set -eu

root=$(cd "$(dirname "$0")/../.." && pwd)
installer="$root/scripts/install-macos.sh"
package_script="$root/scripts/package-macos.sh"

[ -f "$installer" ] || { echo "macOS installer does not exist: $installer" >&2; exit 1; }
[ -f "$package_script" ] || { echo "macOS package script does not exist: $package_script" >&2; exit 1; }

assert_eq() {
    expected=$1
    actual=$2
    message=$3
    [ "$expected" = "$actual" ] || {
        echo "$message Expected '$expected', got '$actual'." >&2
        exit 1
    }
}

assert_file() {
    [ -f "$1" ] || { echo "$2" >&2; exit 1; }
}

test_root=$(mktemp -d "${TMPDIR:-/tmp}/catro-macos-installer-test.XXXXXX")
fixture_root="$test_root/fixtures"
install_parent="$test_root/install-parent"
install_root="$install_parent/Catro.app"
sentinel="$install_parent/sibling-sentinel.txt"
launch_marker="$test_root/app-launched.txt"
server_pid=""

cleanup() {
    if [ -n "$server_pid" ]; then
        kill "$server_pid" >/dev/null 2>&1 || true
        wait "$server_pid" >/dev/null 2>&1 || true
    fi
    rm -rf "$test_root"
}
trap cleanup EXIT HUP INT TERM

make_release() {
    version=$1
    asset_arch=$2
    marker=$3
    executable=${4:-present}
    checksum=${5:-valid}
    release_dir="$fixture_root/download/$version"
    stage="$test_root/stage-$version-$asset_arch"
    app="$stage/Catro.app"
    archive="$release_dir/Catro-macos-$asset_arch.zip"

    mkdir -p "$release_dir" "$app/Contents/MacOS" "$app/Contents/Resources"
    if [ "$executable" = present ]; then
        cat >"$app/Contents/MacOS/Catro" <<'EOF'
#!/bin/sh
if [ -n "${CATRO_TEST_LAUNCH_MARKER:-}" ]; then
    printf launched >"$CATRO_TEST_LAUNCH_MARKER"
fi
EOF
        chmod +x "$app/Contents/MacOS/Catro"
    fi
    printf '%s\n' "$marker" >"$app/Contents/Resources/version.txt"

    (cd "$stage" && ditto -c -k --sequesterRsrc . "$archive")
    case "$checksum" in
        valid)
            digest=$(shasum -a 256 "$archive" | awk '{print $1}')
            printf '%s  %s\n' "$digest" "$(basename "$archive")" >"$archive.sha256"
            ;;
        malformed)
            printf '%s\n' 'not-a-sha256-digest' >"$archive.sha256"
            ;;
        mismatch)
            printf '%064d  %s\n' 0 "$(basename "$archive")" >"$archive.sha256"
            ;;
    esac
}

run_installer() {
    version=$1
    architecture=$2
    fail_after_backup=${3:-}
    set +e
    run_output=$(
        CATRO_RELEASE_BASE_URL="$base_url" \
        CATRO_INSTALL_ROOT="$install_root" \
        CATRO_VERSION="$version" \
        CATRO_TEST_ARCHITECTURE="$architecture" \
        CATRO_TEST_FAIL_AFTER_BACKUP="$fail_after_backup" \
        CATRO_TEST_LAUNCH_MARKER="$launch_marker" \
        sh "$installer" 2>&1
    )
    run_status=$?
    set -e
}

assert_installed_version() {
    expected=$1
    assert_file "$install_root/Contents/MacOS/Catro" 'Installed Catro executable is missing.'
    actual=$(tr -d '\r\n' <"$install_root/Contents/Resources/version.txt")
    assert_eq "$expected" "$actual" 'Installed version marker is wrong.'
}

mkdir -p "$fixture_root" "$install_parent"
printf '%s\n' untouched >"$sentinel"

make_release v1 arm64 v1
make_release v2 arm64 v2
make_release malformed arm64 malformed present malformed
make_release mismatch arm64 mismatch present mismatch
make_release missing-executable arm64 missing missing
make_release intel x64 intel

port=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()')
python3 -m http.server "$port" --bind 127.0.0.1 --directory "$fixture_root" >/dev/null 2>&1 &
server_pid=$!
base_url="http://127.0.0.1:$port"

ready=0
attempt=0
while [ "$attempt" -lt 50 ]; do
    if curl -fsS "$base_url/" >/dev/null 2>&1; then
        ready=1
        break
    fi
    attempt=$((attempt + 1))
    sleep 0.1
done
assert_eq 1 "$ready" 'Loopback fixture server did not become ready.'

run_installer v1 arm64
assert_eq 0 "$run_status" "First install failed: $run_output"
assert_installed_version v1

run_installer v2 arm64
assert_eq 0 "$run_status" "Upgrade failed: $run_output"
assert_installed_version v2

run_installer v1 arm64 1
[ "$run_status" -ne 0 ] || { echo 'Forced replacement failure unexpectedly succeeded.' >&2; exit 1; }
assert_installed_version v2
backup_count=$(find "$install_parent" -maxdepth 1 -type d -name 'Catro.app.backup-*' | wc -l | tr -d ' ')
assert_eq 0 "$backup_count" 'Rollback left a backup directory behind.'

for rejected_version in malformed mismatch missing-executable; do
    run_installer "$rejected_version" arm64
    [ "$run_status" -ne 0 ] || {
        echo "Invalid package '$rejected_version' unexpectedly succeeded." >&2
        exit 1
    }
    assert_installed_version v2
done

run_installer intel x86_64
assert_eq 0 "$run_status" "Intel asset selection failed: $run_output"
assert_installed_version intel

run_installer intel ppc
[ "$run_status" -ne 0 ] || { echo 'Unsupported architecture unexpectedly succeeded.' >&2; exit 1; }
assert_installed_version intel

[ ! -e "$launch_marker" ] || { echo 'Installer launched Catro automatically.' >&2; exit 1; }
assert_eq untouched "$(tr -d '\r\n' <"$sentinel")" 'Installer modified a sibling path.'

if [ -n "${CATRO_TEST_BUILT_APP:-}" ]; then
    assert_file "$CATRO_TEST_BUILT_APP/Contents/MacOS/Catro" 'CI-built Catro.app executable is missing.'
    "$package_script" Release

    host_arch=$(uname -m)
    case "$host_arch" in
        arm64) asset_arch=arm64 ;;
        x86_64) asset_arch=x64 ;;
        *) echo "Unsupported package test architecture: $host_arch" >&2; exit 1 ;;
    esac

    archive="$root/out/dist/Catro-macos-$asset_arch.zip"
    checksum_file="$archive.sha256"
    assert_file "$archive" 'Expected macOS archive is missing.'
    assert_file "$checksum_file" 'Expected macOS checksum is missing.'

    expected_digest=$(awk 'NF == 2 && $1 ~ /^[0-9a-f]{64}$/ { print $1 }' "$checksum_file")
    actual_digest=$(shasum -a 256 "$archive" | awk '{print $1}')
    assert_eq "$expected_digest" "$actual_digest" 'Packaged archive checksum is wrong.'

    package_check="$test_root/package-check"
    mkdir -p "$package_check"
    ditto -x -k "$archive" "$package_check"
    assert_file "$package_check/Catro.app/Contents/MacOS/Catro" 'Archive root does not contain Catro.app.'
    assert_file "$package_check/README.txt" 'Archive root does not contain README.txt.'
    codesign --verify --deep --strict "$package_check/Catro.app"

    archived_arch=$(lipo -archs "$package_check/Catro.app/Contents/MacOS/Catro")
    assert_eq "$host_arch" "$archived_arch" 'Archive executable architecture is wrong.'
    grep -q 'macOS 12.3' "$package_check/README.txt"
    grep -qi 'Gatekeeper' "$package_check/README.txt"
    grep -qi 'voice channels' "$package_check/README.txt"
    grep -qi 'screen' "$package_check/README.txt"
    grep -qi "includes that app's sound" "$package_check/README.txt"
    # CI packages without a service URL; only release packages carry the network config.
    if [ -n "${CATRO_SERVICE_URL:-}" ]; then
        grep -q "\"api_base_url\": \"${CATRO_SERVICE_URL%/}\"" \
            "$package_check/Catro.app/Contents/Resources/catro-network.json"
    else
        [ ! -e "$package_check/Catro.app/Contents/Resources/catro-network.json" ]
    fi
fi

echo 'macOS installer and package tests passed.'
