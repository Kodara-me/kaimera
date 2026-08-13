#!/usr/bin/env bash

set -euo pipefail

project_root="$1"
service_binary="$2"
stage_root="$(mktemp -d)"
trap 'rm -rf "$stage_root"' EXIT

"$project_root/scripts/install-kira-service.sh" \
    --root "$stage_root" \
    --binary "$service_binary" \
    --listen 10.0.0.93 \
    --port 9384 \
    --token "correct horse battery staple" \
    --no-enable

program="$stage_root/usr/local/libexec/kira-service/kira-service"
config="$stage_root/etc/kira-service/kira-service.conf"
token="$stage_root/etc/kira-service/token"
unit="$stage_root/usr/lib/systemd/system/kira-service.service"

[[ -x "$program" ]]
[[ "$(stat -c '%a' "$config")" == "600" ]]
[[ "$(stat -c '%a' "$token")" == "640" ]]
[[ "$(stat -c '%a' "$unit")" == "644" ]]
grep -qx 'KIRA_SERVICE_LISTEN=10.0.0.93' "$config"
grep -qx 'KIRA_SERVICE_PORT=9384' "$config"
grep -qx 'KIRA_SERVICE_TOKEN_FILE=/etc/kira-service/token' "$config"
grep -qx 'correct horse battery staple' "$token"
grep -Fqx 'ExecStart=/usr/local/libexec/kira-service/kira-service --listen ${KIRA_SERVICE_LISTEN} --port ${KIRA_SERVICE_PORT} --token-file ${KIRA_SERVICE_TOKEN_FILE}' "$unit"
! grep -q '@KIRA_SERVICE_GPU_GROUPS@' "$unit"

mkdir -p "$stage_root/var/lib/kira-service/data"
"$project_root/scripts/uninstall-kira-service.sh" --root "$stage_root" --purge --yes
[[ ! -e "$program" ]]
[[ ! -e "$config" ]]
[[ ! -e "$stage_root/var/lib/kira-service" ]]

"$project_root/scripts/install-kira-service.sh" \
    --root "$stage_root" \
    --binary "$service_binary" \
    --listen 10.0.0.93 \
    --port 9384 \
    --token "dry run token" \
    --no-enable \
    --dry-run
[[ ! -e "$stage_root/usr/local/libexec/kira-service/kira-service" ]]
