#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
ENV_FILE="$SCRIPT_DIR/.env"

fail() {
  printf '[catro] install: %s\n' "$*" >&2
  exit 1
}

[[ "${EUID:-$(id -u)}" -eq 0 ]] || fail "run install.sh as root"
[[ -r /etc/os-release ]] || fail "/etc/os-release is unavailable"
# shellcheck disable=SC1091
. /etc/os-release
[[ "${ID:-}" == "ubuntu" ]] || fail "the production bundle targets Ubuntu"

arch="$(uname -m)"
[[ "$arch" == "aarch64" || "$arch" == "x86_64" ]] ||
  fail "unsupported architecture: $arch"
command -v docker >/dev/null 2>&1 || fail "Docker Engine is required"
docker compose version >/dev/null 2>&1 || fail "Docker Compose v2 is required"
command -v openssl >/dev/null 2>&1 || fail "openssl is required"
command -v getent >/dev/null 2>&1 || fail "getent is required"
command -v git >/dev/null 2>&1 || fail "git is required"
command -v ip >/dev/null 2>&1 || fail "iproute2 is required"

if [[ -f "$ENV_FILE" ]]; then
  set -a
  # shellcheck disable=SC1090
  . "$ENV_FILE"
  set +a
fi

CATRO_HOSTNAME="${CATRO_HOSTNAME:-}"
CATRO_PUBLIC_IP="${CATRO_PUBLIC_IP:-}"
CATRO_PRIVATE_IP="${CATRO_PRIVATE_IP:-$(ip -4 route get 1.1.1.1 2>/dev/null | awk '{for (i=1;i<=NF;i++) if ($i=="src") {print $(i+1); exit}}')}"
[[ -n "$CATRO_HOSTNAME" ]] || fail "set CATRO_HOSTNAME to the public DNS hostname"
[[ -n "$CATRO_PUBLIC_IP" ]] || fail "set CATRO_PUBLIC_IP to the VM public IPv4"
[[ -n "$CATRO_PRIVATE_IP" ]] || fail "could not detect CATRO_PRIVATE_IP"

resolved="$(getent ahostsv4 "$CATRO_HOSTNAME" | awk 'NR==1 {print $1}')"
[[ "$resolved" == "$CATRO_PUBLIC_IP" ]] ||
  fail "$CATRO_HOSTNAME resolves to ${resolved:-nothing}, expected $CATRO_PUBLIC_IP"

CATRO_SIGNALING_SECRET="${CATRO_SIGNALING_SECRET:-$(openssl rand -base64 48 | tr -d '\n')}"
CATRO_TURN_SECRET="${CATRO_TURN_SECRET:-$(openssl rand -base64 48 | tr -d '\n')}"
CATRO_MAX_ROOM_PEERS="${CATRO_MAX_ROOM_PEERS:-5}"
CATRO_API_WRITES_PER_MINUTE="${CATRO_API_WRITES_PER_MINUTE:-30}"
CATRO_TURN_MIN_PORT="${CATRO_TURN_MIN_PORT:-49160}"
CATRO_TURN_MAX_PORT="${CATRO_TURN_MAX_PORT:-49200}"
CATRO_IMAGE_TAG="${CATRO_IMAGE_TAG:-prod}"
CATRO_STATE_DIR="${CATRO_STATE_DIR:-/var/lib/catro/signaling}"
CATRO_BACKUP_DIR="${CATRO_BACKUP_DIR:-/var/backups/catro}"
CATRO_CADDY_DATA_DIR="${CATRO_CADDY_DATA_DIR:-/var/lib/catro/caddy-data}"
CATRO_CADDY_CONFIG_DIR="${CATRO_CADDY_CONFIG_DIR:-/var/lib/catro/caddy-config}"

umask 077
mkdir -p   "$CATRO_STATE_DIR"   "$CATRO_BACKUP_DIR"   "$CATRO_CADDY_DATA_DIR"   "$CATRO_CADDY_CONFIG_DIR"
chown 65532:65532 "$CATRO_STATE_DIR"
chmod 0700 "$CATRO_STATE_DIR" "$CATRO_BACKUP_DIR"

state_file="$CATRO_STATE_DIR/directory.json"
if [[ ! -e "$state_file" ]]; then
  printf '{"users":{},"servers":{},"invites":{}}\n' >"$state_file"
fi
chown 65532:65532 "$state_file"
chmod 0600 "$state_file"

cat >"$ENV_FILE" <<EOF
CATRO_HOSTNAME=$CATRO_HOSTNAME
CATRO_PUBLIC_IP=$CATRO_PUBLIC_IP
CATRO_PRIVATE_IP=$CATRO_PRIVATE_IP
CATRO_SIGNALING_SECRET=$CATRO_SIGNALING_SECRET
CATRO_TURN_SECRET=$CATRO_TURN_SECRET
CATRO_MAX_ROOM_PEERS=$CATRO_MAX_ROOM_PEERS
CATRO_API_WRITES_PER_MINUTE=$CATRO_API_WRITES_PER_MINUTE
CATRO_TURN_MIN_PORT=$CATRO_TURN_MIN_PORT
CATRO_TURN_MAX_PORT=$CATRO_TURN_MAX_PORT
CATRO_IMAGE_TAG=$CATRO_IMAGE_TAG
CATRO_STATE_DIR=$CATRO_STATE_DIR
CATRO_BACKUP_DIR=$CATRO_BACKUP_DIR
CATRO_CADDY_DATA_DIR=$CATRO_CADDY_DATA_DIR
CATRO_CADDY_CONFIG_DIR=$CATRO_CADDY_CONFIG_DIR
EOF
chmod 0600 "$ENV_FILE"

ENV_FILE="$ENV_FILE" VERIFY_ONLY=1 "$SCRIPT_DIR/verify.sh"

commit="$(git -C "$REPO_ROOT" rev-parse HEAD)"
printf '[catro] install: source commit %s\n' "$commit"
(
  cd "$SCRIPT_DIR"
  docker compose --env-file "$ENV_FILE" build --pull signaling
  docker compose --env-file "$ENV_FILE" up -d --remove-orphans
)
ENV_FILE="$ENV_FILE" "$SCRIPT_DIR/verify.sh"

snapshot="$REPO_ROOT/out/deploy-active"
rm -rf -- "$snapshot"
mkdir -p "$snapshot"
cp -- "$SCRIPT_DIR/compose.yaml" "$SCRIPT_DIR/Caddyfile" "$SCRIPT_DIR/coturn.conf" "$snapshot/"

for service in signaling caddy coturn; do
  id="$(cd "$SCRIPT_DIR" && docker compose --env-file "$ENV_FILE" ps -q "$service")"
  image_id="$(docker inspect --format '{{.Image}}' "$id")"
  printf '[catro] install: %s image %s\n' "$service" "$image_id"
done
