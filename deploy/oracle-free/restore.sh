#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ENV_FILE="${ENV_FILE:-$SCRIPT_DIR/.env}"
backup="${1:-}"

fail() {
  printf '[catro] restore: %s\n' "$*" >&2
  exit 1
}

[[ -n "$backup" ]] || fail "usage: restore.sh <backup.json>"
[[ -f "$backup" ]] || fail "backup not found: $backup"
[[ -f "$ENV_FILE" ]] || fail "environment file not found: $ENV_FILE"
command -v docker >/dev/null 2>&1 || fail "Docker is required"
docker compose version >/dev/null 2>&1 || fail "Docker Compose v2 is required"
command -v python3 >/dev/null 2>&1 || fail "python3 is required for JSON validation"

set -a
# shellcheck disable=SC1090
. "$ENV_FILE"
set +a

if (
  cd "$SCRIPT_DIR"
  docker compose --env-file "$ENV_FILE" ps --status running --services
) | grep -qx signaling; then
  fail "signaling is running; stop it before restore"
fi

python3 -m json.tool "$backup" >/dev/null ||
  fail "backup is not valid JSON"

mkdir -p "$CATRO_STATE_DIR"
target="$CATRO_STATE_DIR/directory.json"
tmp="$CATRO_STATE_DIR/.directory.restore.$$"
cp -- "$backup" "$tmp"
chown 65532:65532 "$tmp"
chmod 0600 "$tmp"
mv -- "$tmp" "$target"
printf '[catro] restore: restored %s\n' "$target"
