#! /bin/bash
set -e

# Cleanup function to handle script termination
# This function will be called on script exit or interruption
cleanup() {
    pkill -P $$  # Kill all the child processes of the current process group
    # Optional: Delete temporary files
    [ -f "$TMP_FILE" ] && rm "$TMP_FILE"
    exit 1
}

# Register Signal Capture
trap 'cleanup' INT TERM EXIT

./fuzzyl2_bench --benchmark-samples 3 -s > ccs25_balance_l2.log

./fuzzylinf_bench --benchmark-samples 3 -s > ccs25_balance_linf.log

./fuzzyl1_bench --benchmark-samples 3 -s > ccs25_balance_l1.log





