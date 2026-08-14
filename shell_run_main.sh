#!/bin/bash

# Pre-version FPSI benchmark script
#
# LInfPre / L1Pre:
#   d     = 2, 6, 10
#   n     = 8, 12, 16   (set size = 2^n)
#   delta = 10, 60, 250
#
# L2Pre:
#   d     = 2
#   n     = 8, 12, 16
#   delta = 10, 60, 250

BIN="./build/main"
LOG_FILE="OOTEST_$(date +%Y-%m-%d_%H-%M-%S).log"

# Number of repeated trials used by main to calculate average results.
trait=20

# Keep the same benchmark setting as tests.
target_matching_points=29

ns=(8 12 16)
dims=(2 6 10)
deltas=(10 60 250)

print_header() {
  printf "%-7s  %7s  %7s  %2s  %7s  %9s  %9s  %9s  %9s  %9s  %9s\n" \
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

# Clear previous results.
echo "==================== LInfPre ====================" | tee -a "${LOG_FILE}"
print_header

for dim in "${dims[@]}"; do
  for n in "${ns[@]}"; do
    for delta in "${deltas[@]}"; do
      # echo "./build/main -m 0 -n ${n} -d ${dim} -delta ${delta} -i ${target_matching_points} -trait ${trait} -log 0" \
      #   | tee -a "${LOG_FILE}"

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


echo "===================== L1Pre =====================" | tee -a "${LOG_FILE}"
print_header

for dim in "${dims[@]}"; do
  for n in "${ns[@]}"; do
    for delta in "${deltas[@]}"; do
      # echo "./build/main -m 1 -n ${n} -d ${dim} -delta ${delta} -i ${target_matching_points} -trait ${trait} -log 0" \
      #   | tee -a "${LOG_FILE}"

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


echo "===================== L2Pre =====================" | tee -a "${LOG_FILE}"
print_header

dim=2

for n in "${ns[@]}"; do
  for delta in "${deltas[@]}"; do
    # echo "./build/main -m 2 -n ${n} -d ${dim} -delta ${delta} -i ${target_matching_points} -trait ${trait} -log 0" \
    #   | tee -a "${LOG_FILE}"

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

echo "=================================================" | tee -a "${LOG_FILE}"
echo "All benchmarks finished. Results: ${LOG_FILE}" | tee -a "${LOG_FILE}"
