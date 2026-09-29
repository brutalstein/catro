#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ENV_FILE="${ENV_FILE:-$SCRIPT_DIR/.env}"

[[ -f "$ENV_FILE" ]] || {
  printf '[catro] backup: missing %s\n' "$ENV_FILE" >&2
  exit 1
}
set -a
# shellcheck disable=SC1090
. "$ENV_FILE"
set +a

state="$CATRO_STATE_DIR/directory.json"
[[ -f "$state" ]] || {
  printf '[catro] backup: state file does not exist: %s\n' "$state" >&2
  exit 1
}
mkdir -p "$CATRO_BACKUP_DIR"
chmod 0700 "$CATRO_BACKUP_DIR"

stamp="$(date -u +%Y%m%dT%H%M%SZ)"
tmp="$CATRO_BACKUP_DIR/.directory-$stamp.json.tmp"
dest="$CATRO_BACKUP_DIR/directory-$stamp.json"
cp -- "$state" "$tmp"
chmod 0600 "$tmp"
mv -- "$tmp" "$dest"

mapfile -t backups < <(
  find "$CATRO_BACKUP_DIR" -maxdepth 1 -type f -name 'directory-*.json'     -printf '%T@ %p\n' | sort -nr | cut -d' ' -f2-
)
if (( ${#backups[@]} > 7 )); then
  for old in "${backups[@]:7}"; do
    rm -- "$old"
  done
fi

printf '[catro] backup: %s\n' "$dest"
