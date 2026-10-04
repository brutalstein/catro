#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ENV_FILE="${ENV_FILE:-$SCRIPT_DIR/.env}"
VERIFY_ONLY="${VERIFY_ONLY:-0}"

fail() {
  printf '[catro] verify: %s\n' "$*" >&2
  exit 1
}

if [[ ! -f "$ENV_FILE" ]]; then
  if [[ "$VERIFY_ONLY" == "1" && -f "$SCRIPT_DIR/.env.example" ]]; then
    ENV_FILE="$SCRIPT_DIR/.env.example"
    printf '[catro] verify: using .env.example for static validation only\n'
  else
    fail "environment file not found: $ENV_FILE"
  fi
fi

set -a
# shellcheck disable=SC1090
. "$ENV_FILE"
set +a

# Backward-compatible deployment migration: older protected .env files predate the independent
# discovery limiter. Compose and the signaling binary use the same safe default.
CATRO_DISCOVERY_READS_PER_MINUTE="${CATRO_DISCOVERY_READS_PER_MINUTE:-30}"

required=(
  CATRO_HOSTNAME CATRO_PUBLIC_IP CATRO_PRIVATE_IP
  CATRO_SIGNALING_SECRET CATRO_TURN_SECRET
  CATRO_MAX_ROOM_PEERS CATRO_API_WRITES_PER_MINUTE
  CATRO_TURN_MIN_PORT CATRO_TURN_MAX_PORT
  CATRO_IMAGE_TAG CATRO_STATE_DIR CATRO_BACKUP_DIR
  CATRO_CADDY_DATA_DIR CATRO_CADDY_CONFIG_DIR
)
for name in "${required[@]}"; do
  [[ -n "${!name:-}" ]] || fail "$name is required"
done

[[ ${#CATRO_SIGNALING_SECRET} -ge 32 ]] ||
  fail "CATRO_SIGNALING_SECRET must be at least 32 bytes"
[[ ${#CATRO_TURN_SECRET} -ge 32 ]] ||
  fail "CATRO_TURN_SECRET must be at least 32 bytes"
[[ "$CATRO_MAX_ROOM_PEERS" =~ ^[0-9]+$ ]] ||
  fail "CATRO_MAX_ROOM_PEERS must be numeric"
(( CATRO_MAX_ROOM_PEERS >= 2 && CATRO_MAX_ROOM_PEERS <= 5 )) ||
  fail "CATRO_MAX_ROOM_PEERS must be between 2 and 5"
[[ "$CATRO_API_WRITES_PER_MINUTE" =~ ^[0-9]+$ ]] ||
  fail "CATRO_API_WRITES_PER_MINUTE must be numeric"
(( CATRO_API_WRITES_PER_MINUTE >= 1 && CATRO_API_WRITES_PER_MINUTE <= 300 )) ||
  fail "CATRO_API_WRITES_PER_MINUTE must be between 1 and 300"
[[ "$CATRO_DISCOVERY_READS_PER_MINUTE" =~ ^[0-9]+$ ]] ||
  fail "CATRO_DISCOVERY_READS_PER_MINUTE must be numeric"
(( CATRO_DISCOVERY_READS_PER_MINUTE >= 1 &&
   CATRO_DISCOVERY_READS_PER_MINUTE <= 300 )) ||
  fail "CATRO_DISCOVERY_READS_PER_MINUTE must be between 1 and 300"
[[ "$CATRO_TURN_MIN_PORT" =~ ^[0-9]+$ && "$CATRO_TURN_MAX_PORT" =~ ^[0-9]+$ ]] ||
  fail "TURN relay ports must be numeric"
(( CATRO_TURN_MIN_PORT >= 1024 &&
   CATRO_TURN_MAX_PORT <= 65535 &&
   CATRO_TURN_MIN_PORT < CATRO_TURN_MAX_PORT )) ||
  fail "TURN relay port range is invalid"
[[ "$CATRO_STATE_DIR" != "$CATRO_BACKUP_DIR" ]] ||
  fail "state and backup directories must be different"

ipv4_re='^([0-9]{1,3}\.){3}[0-9]{1,3}$'
[[ "$CATRO_PUBLIC_IP" =~ $ipv4_re ]] || fail "CATRO_PUBLIC_IP must be IPv4"
[[ "$CATRO_PRIVATE_IP" =~ $ipv4_re ]] || fail "CATRO_PRIVATE_IP must be IPv4"

for file in compose.yaml Caddyfile coturn.conf; do
  [[ -f "$SCRIPT_DIR/$file" ]] || fail "missing $file"
done

if grep -Eq 'image:[[:space:]]+[^#[:space:]]*:latest([[:space:]]|$)' "$SCRIPT_DIR/compose.yaml"; then
  fail "compose.yaml must not use latest image tags"
fi

signaling_block="$(
  awk '
    /^  signaling:/ {inside=1; next}
    /^  [A-Za-z0-9_-]+:/ {if (inside) exit}
    inside {print}
  ' "$SCRIPT_DIR/compose.yaml"
)"
if grep -Eq '^[[:space:]]+ports:' <<<"$signaling_block"; then
  fail "signaling must not publish a host port"
fi

if grep -Fq '/metrics' "$SCRIPT_DIR/Caddyfile"; then
  fail "Caddyfile must not expose the private metrics route"
fi

grep -Fq 'caddy:2.11.4-alpine' "$SCRIPT_DIR/compose.yaml" ||
  fail "Caddy image is not pinned to the approved version"
grep -Fq 'coturn/coturn:4.18.0-r0' "$SCRIPT_DIR/compose.yaml" ||
  fail "coturn image is not pinned to the approved version"

compose_available=0
if command -v docker >/dev/null 2>&1 &&
   docker compose version >/dev/null 2>&1; then
  compose_available=1
  (
    cd "$SCRIPT_DIR"
    docker compose --env-file "$ENV_FILE" config >/dev/null
  ) || fail "docker compose config failed"
else
  if [[ "$VERIFY_ONLY" != "1" ]]; then
    fail "Docker Compose v2 is required"
  fi
  printf '[catro] verify: Docker Compose unavailable; config gate not executed\n'
fi

if [[ "$VERIFY_ONLY" == "1" ]]; then
  printf '[catro] verify: static validation passed'
  if (( compose_available == 1 )); then
    printf ' (including docker compose config)'
  fi
  printf '\n'
  exit 0
fi

mode="$(stat -c '%a' "$ENV_FILE")"
[[ "$mode" == "600" ]] || fail "$ENV_FILE must have mode 0600 (found $mode)"

wait_healthy() {
  local service="$1"
  local id status
  for _ in {1..60}; do
    id="$(cd "$SCRIPT_DIR" && docker compose --env-file "$ENV_FILE" ps -q "$service")"
    if [[ -n "$id" ]]; then
      status="$(docker inspect --format '{{if .State.Health}}{{.State.Health.Status}}{{else}}{{.State.Status}}{{end}}' "$id" 2>/dev/null || true)"
      if [[ "$status" == "healthy" ]]; then
        return 0
      fi
      if [[ "$status" == "unhealthy" || "$status" == "exited" || "$status" == "dead" ]]; then
        return 1
      fi
    fi
    sleep 2
  done
  return 1
}

# A shared-edge host runs its own reverse proxy instead of the bundled Caddy (see update.sh).
services=(signaling coturn caddy)
if [[ -f "$SCRIPT_DIR/compose.shared-edge.yaml" ]]; then
  export COMPOSE_FILE="$SCRIPT_DIR/compose.yaml:$SCRIPT_DIR/compose.shared-edge.yaml"
  services=(signaling coturn)
fi

for service in "${services[@]}"; do
  printf '[catro] verify: waiting for %s health\n' "$service"
  wait_healthy "$service" || fail "$service did not become healthy"
done

printf '[catro] verify: deployment is healthy\n'
