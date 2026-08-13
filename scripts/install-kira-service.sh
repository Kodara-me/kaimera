#!/usr/bin/env bash

set -euo pipefail

readonly SERVICE_NAME="kira-service"
readonly SERVICE_USER="kira-service"
readonly SERVICE_GROUP="kira-service"
readonly DEFAULT_PORT=9384

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd -- "$script_dir/.." && pwd)"
binary_path="$project_root/build/src/kira-service"
listen_address=""
port="$DEFAULT_PORT"
token=""
install_root=""
dry_run=false
enable_service=true

usage() {
    cat <<'EOF'
Usage: sudo scripts/install-kira-service.sh [options]

Install the Kira Service systemd unit. By default, missing connection values
are requested interactively.

Options:
  --binary PATH       Built kira-service executable (default: build/src/kira-service)
  --listen ADDRESS    LAN address to bind, for example 10.0.0.93
  --port PORT         WebSocket port (default: 9384)
  --token TOKEN       Shared LAN passphrase; avoid this option in a shared shell history
  --root PATH         Stage files under PATH instead of the live filesystem
  --dry-run           Print actions without modifying the filesystem
  --no-enable         Install without enabling or starting the systemd service
  -h, --help          Show this help
EOF
}

fail() {
    printf 'Error: %s\n' "$*" >&2
    exit 1
}

run() {
    if "$dry_run"; then
        printf '+ '
        printf '%q ' "$@"
        printf '\n'
        return
    fi
    "$@"
}

destination() {
    printf '%s%s' "$install_root" "$1"
}

valid_port() {
    [[ "$1" =~ ^[0-9]+$ ]] && (( $1 >= 1 && $1 <= 65535 ))
}

valid_address() {
    [[ "$1" =~ ^[0-9A-Fa-f:.]+$ ]]
}

while (( $# > 0 )); do
    case "$1" in
    --binary) binary_path="${2:-}"; shift 2 ;;
    --listen) listen_address="${2:-}"; shift 2 ;;
    --port) port="${2:-}"; shift 2 ;;
    --token) token="${2:-}"; shift 2 ;;
    --root) install_root="${2:-}"; shift 2 ;;
    --dry-run) dry_run=true; shift ;;
    --no-enable) enable_service=false; shift ;;
    -h|--help) usage; exit 0 ;;
    *) fail "Unknown option: $1" ;;
    esac
done

[[ -n "$install_root" || $EUID -eq 0 ]] || fail "Run with sudo, or use --root for a staging installation."
[[ -x "$binary_path" ]] || fail "Kira Service executable is not executable: $binary_path"
[[ -f "$project_root/packaging/systemd/kira-service.service.in" ]] || fail "Systemd unit template is missing."

if [[ -z "$listen_address" && -t 0 ]]; then
    read -r -p "LAN address to bind (for example 10.0.0.93): " listen_address
fi
if [[ -z "$listen_address" ]]; then
    fail "Provide --listen ADDRESS when input is not interactive."
fi
valid_address "$listen_address" || fail "The listen address contains unsupported characters."

if [[ "$port" == "$DEFAULT_PORT" && -t 0 ]]; then
    read -r -p "WebSocket port [$DEFAULT_PORT]: " entered_port
    port="${entered_port:-$DEFAULT_PORT}"
fi
valid_port "$port" || fail "Port must be between 1 and 65535."

if [[ -z "$token" && -t 0 ]]; then
    read -r -s -p "Shared LAN passphrase: " token
    printf '\n'
fi
[[ -n "$token" ]] || fail "A non-empty shared LAN passphrase is required."
[[ "$token" != *$'\n'* && "$token" != *$'\r'* ]] || fail "The passphrase must be one line."

if [[ -z "$install_root" ]]; then
    if ! getent group "$SERVICE_GROUP" >/dev/null; then
        run groupadd --system "$SERVICE_GROUP"
    fi
    if ! id -u "$SERVICE_USER" >/dev/null 2>&1; then
        run useradd --system --gid "$SERVICE_GROUP" --home-dir "/var/lib/$SERVICE_NAME" \
            --create-home --shell /usr/sbin/nologin "$SERVICE_USER"
    fi
    for gpu_group in render video; do
        if getent group "$gpu_group" >/dev/null; then
            run usermod --append --groups "$gpu_group" "$SERVICE_USER"
        fi
    done
fi

program_dir="$(destination "/usr/local/libexec/$SERVICE_NAME")"
config_dir="$(destination "/etc/$SERVICE_NAME")"
unit_dir="$(destination "/usr/lib/systemd/system")"
state_dir="$(destination "/var/lib/$SERVICE_NAME")"
token_path="$(destination "/etc/$SERVICE_NAME/token")"
config_path="$(destination "/etc/$SERVICE_NAME/$SERVICE_NAME.conf")"
unit_path="$(destination "/usr/lib/systemd/system/$SERVICE_NAME.service")"

run install -d -m 0755 "$program_dir" "$config_dir" "$unit_dir"
run install -d -m 0750 "$state_dir"
run install -m 0755 "$binary_path" "$program_dir/$SERVICE_NAME"

if [[ -z "$install_root" ]]; then
    run chown "$SERVICE_USER:$SERVICE_GROUP" "$state_dir"
fi

if "$dry_run"; then
    printf '+ write %q\n' "$config_path"
else
    umask 0077
    printf 'KIRA_SERVICE_LISTEN=%s\nKIRA_SERVICE_PORT=%s\nKIRA_SERVICE_TOKEN_FILE=/etc/%s/token\n' \
        "$listen_address" "$port" "$SERVICE_NAME" > "$config_path"
    printf '%s\n' "$token" > "$token_path"
    chmod 0600 "$config_path"
    chmod 0640 "$token_path"
fi
if [[ -z "$install_root" ]]; then
    run chown root:root "$config_path"
    run chown "root:$SERVICE_GROUP" "$token_path"
fi

gpu_groups=""
if [[ -z "$install_root" ]]; then
    for gpu_group in render video; do
        if getent group "$gpu_group" >/dev/null; then
            gpu_groups+=" $gpu_group"
        fi
    done
fi
gpu_line=""
if [[ -n "$gpu_groups" ]]; then
    gpu_line="SupplementaryGroups=${gpu_groups# }"
fi
if "$dry_run"; then
    printf '+ render %q\n' "$unit_path"
else
    sed "s|@KIRA_SERVICE_GPU_GROUPS@|$gpu_line|" \
        "$project_root/packaging/systemd/$SERVICE_NAME.service.in" > "$unit_path"
    chmod 0644 "$unit_path"
fi

if [[ -z "$install_root" ]] && command -v systemd-analyze >/dev/null 2>&1; then
    run systemd-analyze verify "$unit_path"
fi

if [[ -z "$install_root" && "$enable_service" == true ]]; then
    run systemctl daemon-reload
    run systemctl enable --now "$SERVICE_NAME.service"
fi

subnet=""
if command -v ip >/dev/null 2>&1; then
    subnet="$(ip -o -4 addr show 2>/dev/null | awk -v address="$listen_address" '$4 ~ ("^" address "/") { print $4; exit }')"
fi
printf '\nInstalled Kira Service.\n'
printf 'Manage it with: sudo systemctl status|restart|stop|enable --now %s\n' "$SERVICE_NAME"
printf 'Logs: journalctl -u %s -f\n' "$SERVICE_NAME"
printf 'Firewall rules were not changed. Allow TCP port %s only from your LAN' "$port"
if [[ -n "$subnet" ]]; then
    printf ' (%s)' "$subnet"
fi
printf '.\n'
