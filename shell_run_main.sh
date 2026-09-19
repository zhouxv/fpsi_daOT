#!/usr/bin/env bash
set -euo pipefail

# ============================================================
# Pre-version FPSI benchmark script
#
# Usage:
#   ./fpsi_bench.sh quick
#   ./fpsi_bench.sh full
#
# Default:
#   full
#
# Metrics:
#   m = 0: LInfPre
#   m = 1: L1Pre
#   m = 2: L2Pre
#
# Full:
#   LInfPre / L1Pre:
#     d     = 2, 6, 10
#     n     = 8, 12, 16
#     delta = 10, 60, 250
#     trait = 3
#
#   L2Pre:
#     d     = 2
#     n     = 8, 12, 16
#     delta = 10, 60, 250
#
# Quick:
#   LInfPre / L1Pre:
#     d     = 2, 6, 10
#     n     = 12
#     delta = 10, 250
#     trait = 1
#
#   L2Pre:
#     d     = 2
#     n     = 12
#     delta = 10, 250
#
# Optional overrides:
#   -trait N
#   -i N
#   -m VALUES...
#   -n VALUES...
#   -d VALUES...
#   -delta VALUES...
# ============================================================

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

BIN="${SCRIPT_DIR}/build/main"

mode="full"
target_matching_points=29

# ============================================================
# Read benchmark mode.
# ============================================================

if [[ $# -gt 0 ]]; then
  case "$1" in
    quick|full)
      mode="$1"
      shift
      ;;
    -h|--help)
      cat <<EOF
Usage:
  ${0##*/} [quick|full] [options]

Modes:

  quick
    metric = 0 1 2
    n      = 12
    d      = 2 6 10
    delta  = 10 250
    trait  = 1

    Note:
      L2Pre only runs with d=2.

  full
    metric = 0 1 2
    n      = 8 12 16
    d      = 2 6 10
    delta  = 10 60 250
    trait  = 3

    Note:
      L2Pre only runs with d=2.

Options:
  -trait N
  -i N
  -m VALUES...
  -n VALUES...
  -d VALUES...
  -delta VALUES...

Examples:

  ${0##*/} quick

  ${0##*/} full

  ${0##*/} quick -m 0 1

  ${0##*/} full -trait 10

EOF
      exit 0
      ;;
  esac
fi

# ============================================================
# Set defaults according to the selected mode.
# ============================================================

case "${mode}" in
  quick)
    trait=1
    ms=(0 1 2)
    ns=(12)
    dims=(2 6 10)
    deltas=(10 250)
    ;;

  full)
    trait=3
    ms=(0 1 2)
    ns=(8 12 16)
    dims=(2 6 10)
    deltas=(10 60 250)
    ;;

  *)
    echo "Invalid mode: ${mode}" >&2
    echo "Expected: quick or full" >&2
    exit 1
    ;;
esac

# ============================================================
# Parse optional command-line overrides.
# ============================================================

while [[ $# -gt 0 ]]; do
  case "$1" in

    -trait)
      trait="$2"
      shift 2
      ;;

    -i)
      target_matching_points="$2"
      shift 2
      ;;

    -m)
      shift
      ms=()
      while [[ $# -gt 0 && "$1" != -* ]]; do
        ms+=("$1")
        shift
      done
      ;;

    -n)
      shift
      ns=()
      while [[ $# -gt 0 && "$1" != -* ]]; do
        ns+=("$1")
        shift
      done
      ;;

    -d)
      shift
      dims=()
      while [[ $# -gt 0 && "$1" != -* ]]; do
        dims+=("$1")
        shift
      done
      ;;

    -delta)
      shift
      deltas=()
      while [[ $# -gt 0 && "$1" != -* ]]; do
        deltas+=("$1")
        shift
      done
      ;;

    -h|--help)
      echo "Usage:"
      echo "  ${0##*/} [quick|full] [options]"
      echo
      echo "Options:"
      echo "  -trait N"
      echo "  -i N"
      echo "  -m VALUES..."
      echo "  -n VALUES..."
      echo "  -d VALUES..."
      echo "  -delta VALUES..."
      exit 0
      ;;

    *)
      echo "Unknown argument: $1" >&2
      exit 1
      ;;
  esac
done

# ============================================================
# Check binary.
# ============================================================

if [[ ! -x "${BIN}" ]]; then
  echo "Error: benchmark binary not found or not executable:" >&2
  echo "  ${BIN}" >&2
  exit 1
fi

# ============================================================
# Log file.
# ============================================================

LOG_FILE="${SCRIPT_DIR}/OOTEST_${mode}_$(date +%Y-%m-%d_%H-%M-%S).log"

# ============================================================
# Print benchmark information.
# ============================================================

echo "Pre-version FPSI Benchmark" | tee -a "${LOG_FILE}"
echo "Mode     : ${mode}" | tee -a "${LOG_FILE}"
echo "Metrics  : ${ms[*]}" | tee -a "${LOG_FILE}"
echo "n        : ${ns[*]}" | tee -a "${LOG_FILE}"
echo "d        : ${dims[*]}" | tee -a "${LOG_FILE}"
echo "delta    : ${deltas[*]}" | tee -a "${LOG_FILE}"
echo "trait    : ${trait}" | tee -a "${LOG_FILE}"
echo "matches  : ${target_matching_points}" | tee -a "${LOG_FILE}"
echo "Log      : ${LOG_FILE}" | tee -a "${LOG_FILE}"
echo | tee -a "${LOG_FILE}"

# ============================================================
# Result header.
# ============================================================

print_header() {
  printf "%-7s  %7s  %7s  %2s  %7s  %12s  %12s  %12s  %12s  %12s  %12s\n" \
    "" \
    "N" \
    "Metric" \
    "d" \
    "delta" \
    "Off.Com(MB)" \
    "Off.Time(s)" \
    "On.Com(MB)" \
    "On.Time(s)" \
    "Total.Com" \
    "Total.Time" \
    | tee -a "${LOG_FILE}"
}

# ============================================================
# Run benchmarks.
# ============================================================

for metric in "${ms[@]}"; do

  case "${metric}" in

    # --------------------------------------------------------
    # LInfPre
    # --------------------------------------------------------
    0)
      echo "==================== LInfPre ====================" \
        | tee -a "${LOG_FILE}"

      print_header

      for dim in "${dims[@]}"; do
        for n in "${ns[@]}"; do
          for delta in "${deltas[@]}"; do

            "${BIN}" \
              -m 0 \
              -n "${n}" \
              -d "${dim}" \
              -delta "${delta}" \
              -i "${target_matching_points}" \
              -trait "${trait}" \
              -log 0 \
              | tee -a "${LOG_FILE}"

          done
        done
      done
      ;;

    # --------------------------------------------------------
    # L1Pre
    # --------------------------------------------------------
    1)
      echo "===================== L1Pre =====================" \
        | tee -a "${LOG_FILE}"

      print_header

      for dim in "${dims[@]}"; do
        for n in "${ns[@]}"; do
          for delta in "${deltas[@]}"; do

            "${BIN}" \
              -m 1 \
              -n "${n}" \
              -d "${dim}" \
              -delta "${delta}" \
              -i "${target_matching_points}" \
              -trait "${trait}" \
              -log 0 \
              | tee -a "${LOG_FILE}"

          done
        done
      done
      ;;

    # --------------------------------------------------------
    # L2Pre
    #
    # L2Pre only benchmarks d=2.
    # --------------------------------------------------------
    2)
      echo "===================== L2Pre =====================" \
        | tee -a "${LOG_FILE}"

      print_header

      dim=2

      for n in "${ns[@]}"; do
        for delta in "${deltas[@]}"; do

          "${BIN}" \
            -m 2 \
            -n "${n}" \
            -d "${dim}" \
            -delta "${delta}" \
            -i "${target_matching_points}" \
            -trait "${trait}" \
            -log 0 \
            | tee -a "${LOG_FILE}"

        done
      done
      ;;

    *)
      echo "Invalid metric: ${metric}. Supported values: 0, 1, 2." >&2
      exit 1
      ;;

  esac
done

# ============================================================
# Done.
# ============================================================

echo | tee -a "${LOG_FILE}"
echo "All benchmarks finished." | tee -a "${LOG_FILE}"
echo "Results: ${LOG_FILE}" | tee -a "${LOG_FILE}"