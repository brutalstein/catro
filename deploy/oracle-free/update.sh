#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
ENV_FILE="${ENV_FILE:-$SCRIPT_DIR/.env}"
ACTIVE_SNAPSHOT="$REPO_ROOT/out/deploy-active"

fail() {
  printf '[catro] update: %s\n' "$*" >&2
  exit 1
}

[[ -f "$ENV_FILE" ]] || fail "environment file not found: $ENV_FILE"
command -v git >/dev/null 2>&1 || fail "git is required"
command -v docker >/dev/null 2>&1 || fail "Docker is required"
docker compose version >/dev/null 2>&1 || fail "Docker Compose v2 is required"

[[ -z "$(git -C "$REPO_ROOT" status --porcelain)" ]] ||
  fail "repository must be clean before update"
commit="$(git -C "$REPO_ROOT" rev-parse HEAD)"

set -a
# shellcheck disable=SC1090
. "$ENV_FILE"
set +a

ENV_FILE="$ENV_FILE" VERIFY_ONLY=1 "$SCRIPT_DIR/verify.sh"
"$SCRIPT_DIR/backup.sh" >/dev/null

image="catro-signaling:$CATRO_IMAGE_TAG"
rollback_tag="catro-signaling:rollback-$(date -u +%Y%m%dT%H%M%SZ)"
had_previous=0
if docker image inspect "$image" >/dev/null 2>&1; then
  docker tag "$image" "$rollback_tag"
  had_previous=1
fi

printf '[catro] update: source commit %s\n' "$commit"
if (( had_previous == 1 )); then
  printf '[catro] update: rollback image %s -> %s\n'     "$(docker image inspect "$rollback_tag" --format '{{.Id}}')" "$rollback_tag"
fi

rollback() {
  rc=$?
  if (( rc == 0 )); then
    return
  fi
  printf '[catro] update: deployment failed; attempting previous deployment rollback\n' >&2
  if (( had_previous == 1 )); then
    docker tag "$rollback_tag" "$image"
  fi
  if [[ -f "$ACTIVE_SNAPSHOT/compose.yaml" &&
        -f "$ACTIVE_SNAPSHOT/Caddyfile" &&
        -f "$ACTIVE_SNAPSHOT/coturn.conf" ]]; then
    (
      cd "$ACTIVE_SNAPSHOT"
      docker compose -f "$ACTIVE_SNAPSHOT/compose.yaml" --env-file "$ENV_FILE"         up -d --force-recreate --remove-orphans
    ) || true
    ENV_FILE="$ENV_FILE" "$SCRIPT_DIR/verify.sh" || true
  elif (( had_previous == 1 )); then
    (
      cd "$SCRIPT_DIR"
      docker compose --env-file "$ENV_FILE" up -d --force-recreate --remove-orphans
    ) || true
    ENV_FILE="$ENV_FILE" "$SCRIPT_DIR/verify.sh" || true
  fi
  exit "$rc"
}
trap rollback EXIT

(
  cd "$SCRIPT_DIR"
  docker compose --env-file "$ENV_FILE" build --pull signaling
  docker compose --env-file "$ENV_FILE" up -d --force-recreate --remove-orphans
)
ENV_FILE="$ENV_FILE" "$SCRIPT_DIR/verify.sh"

rm -rf -- "$ACTIVE_SNAPSHOT"
mkdir -p "$ACTIVE_SNAPSHOT"
cp -- "$SCRIPT_DIR/compose.yaml" "$SCRIPT_DIR/Caddyfile" "$SCRIPT_DIR/coturn.conf" "$ACTIVE_SNAPSHOT/"

trap - EXIT
if (( had_previous == 1 )); then
  docker image rm "$rollback_tag" >/dev/null 2>&1 || true
fi

for service in signaling caddy coturn; do
  id="$(cd "$SCRIPT_DIR" && docker compose --env-file "$ENV_FILE" ps -q "$service")"
  printf '[catro] update: %s image %s\n'     "$service" "$(docker inspect --format '{{.Image}}' "$id")"
done
