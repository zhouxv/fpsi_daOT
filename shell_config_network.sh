#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
TCCONFIG_BIN_DIR="${SCRIPT_DIR}/install/tcconfig/bin"

usage() {
  cat <<'EOF'
Usage:
  shell_config_network.sh lan [--interface IFACE]
  shell_config_network.sh wan [--interface IFACE]
  shell_config_network.sh custom --rate RATE [--rtt TIME] [--interface IFACE]
  shell_config_network.sh show [--interface IFACE]
  shell_config_network.sh detect [--interface IFACE]
  shell_config_network.sh metadata [--interface IFACE]
  shell_config_network.sh clear [--interface IFACE]
  shell_config_network.sh -h|--help

Profiles:
  lan      10Gbps bandwidth, 0ms RTT
  wan      100Mbps bandwidth, 80ms RTT
  custom   User-provided bandwidth and RTT; benchmark label is "unknown"

Commands:
  show      Print the detected profile and tcconfig rules
  detect    Print only lan, wan, or unknown
  metadata  Print tab-separated: profile, interface, rate, RTT
  clear     Delete all tcconfig rules from the interface

Examples:
  ./shell_config_network.sh lan
  ./shell_config_network.sh wan
  ./shell_config_network.sh custom --rate 1Gbps --rtt 20ms
  ./shell_config_network.sh show

Configuration commands require root or sudo and the NET_ADMIN capability.
tcconfig defines --delay as round-trip network delay (RTT).
EOF
}

require_command() {
  local name="$1"
  if [[ -x "${TCCONFIG_BIN_DIR}/${name}" ]]; then
    printf '%s\n' "${TCCONFIG_BIN_DIR}/${name}"
  elif command -v "${name}" >/dev/null 2>&1; then
    command -v "${name}"
  else
    printf 'Error: %s was not found. Run ./shell_install_dependencies.sh first.\n' \
      "${name}" >&2
    exit 1
  fi
}

run_privileged() {
  if [[ "${EUID}" -eq 0 ]]; then
    "$@"
  elif command -v sudo >/dev/null 2>&1; then
    sudo "$@"
  else
    printf 'Error: this command requires root or sudo (NET_ADMIN).\n' >&2
    exit 1
  fi
}

normalize_unit() {
  printf '%s' "$1" | tr '[:upper:]' '[:lower:]' | tr -d '[:space:]'
}

read_metadata() {
  local interface="$1" tcshow_bin json rate rtt profile rate_norm rtt_norm
  tcshow_bin="$(require_command tcshow)"
  json="$("${tcshow_bin}" "${interface}" 2>/dev/null || true)"

  if command -v jq >/dev/null 2>&1 && [[ -n "${json}" ]]; then
    rate="$(jq -r '[.. | objects | .rate? // empty][0] // "unconfigured"' \
      <<<"${json}" 2>/dev/null || printf 'unconfigured')"
    rtt="$(jq -r '[.. | objects | .delay? // empty][0] // "0ms"' \
      <<<"${json}" 2>/dev/null || printf '0ms')"
  else
    rate="unconfigured"
    rtt="unconfigured"
  fi

  rate_norm="$(normalize_unit "${rate}")"
  rtt_norm="$(normalize_unit "${rtt}")"
  profile="unknown"

  if [[ "${rate_norm}" =~ ^10(\.0+)?gbps$ ]] &&
     [[ "${rtt_norm}" =~ ^0(\.0+)?(ms|msec|milliseconds?)?$ ]]; then
    profile="lan"
  elif [[ "${rate_norm}" =~ ^100(\.0+)?mbps$ ]] &&
       [[ "${rtt_norm}" =~ ^80(\.0+)?(ms|msec|milliseconds?)$ ]]; then
    profile="wan"
  fi

  printf '%s\t%s\t%s\t%s\n' "${profile}" "${interface}" "${rate}" "${rtt}"
}

if [[ $# -eq 0 ]]; then
  usage
  exit 1
fi

command_name="$1"
shift

if [[ "${command_name}" == "-h" || "${command_name}" == "--help" ]]; then
  usage
  exit 0
fi

interface="lo"
rate=""
rtt="0ms"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --interface)
      [[ $# -ge 2 ]] || { echo 'Error: --interface requires a value.' >&2; exit 1; }
      interface="$2"
      shift 2
      ;;
    --rate)
      [[ $# -ge 2 ]] || { echo 'Error: --rate requires a value.' >&2; exit 1; }
      rate="$2"
      shift 2
      ;;
    --rtt)
      [[ $# -ge 2 ]] || { echo 'Error: --rtt requires a value.' >&2; exit 1; }
      rtt="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      printf 'Error: unknown option: %s\n\n' "$1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

case "${command_name}" in
  lan)
    rate="10Gbps"
    rtt="0ms"
    ;;
  wan)
    rate="100Mbps"
    rtt="80ms"
    ;;
  custom)
    if [[ -z "${rate}" ]]; then
      echo 'Error: custom requires --rate RATE.' >&2
      exit 1
    fi
    ;;
  show|detect|metadata|clear)
    if [[ -n "${rate}" || "${rtt}" != "0ms" ]]; then
      printf 'Error: --rate and --rtt are only valid with custom.\n' >&2
      exit 1
    fi
    ;;
  *)
    printf 'Error: unknown command: %s\n\n' "${command_name}" >&2
    usage >&2
    exit 1
    ;;
esac

case "${command_name}" in
  lan|wan|custom)
    tcset_bin="$(require_command tcset)"
    run_privileged "${tcset_bin}" "${interface}" --rate "${rate}" \
      --delay "${rtt}" --overwrite
    echo
    "$0" show --interface "${interface}"
    ;;
  clear)
    tcdel_bin="$(require_command tcdel)"
    run_privileged "${tcdel_bin}" "${interface}" --all
    printf 'Cleared tcconfig rules on %s.\n' "${interface}"
    ;;
  detect)
    metadata="$(read_metadata "${interface}")"
    IFS=$'\t' read -r profile _ <<<"${metadata}"
    printf '%s\n' "${profile}"
    ;;
  metadata)
    read_metadata "${interface}"
    ;;
  show)
    metadata="$(read_metadata "${interface}")"
    IFS=$'\t' read -r profile detected_interface detected_rate detected_rtt \
      <<<"${metadata}"
    printf 'Detected network configuration:\n'
    printf '  Profile   : %s\n' "${profile}"
    printf '  Interface : %s\n' "${detected_interface}"
    printf '  Rate      : %s\n' "${detected_rate}"
    printf '  RTT       : %s\n' "${detected_rtt}"
    printf '\ntcconfig rules:\n'
    tcshow_bin="$(require_command tcshow)"
    "${tcshow_bin}" "${interface}"
    ;;
esac
