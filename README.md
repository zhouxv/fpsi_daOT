# Fuzzy PSI

This project implements the Fuzzy PSI protocols presented in [Distance-Aware OT with Application to Fuzzy PSI](https://eprint.iacr.org/2025/996).

### Running Benchmark Collections

We provide a convenience script `shell_run_bench.sh` to automatically run both L∞ and L1 Fuzzy PSI benchmarks and save their outputs.

## 1. Benchmark Executables

Our executables are placed inside the `/home/build` directory. The following benchmark executables are available:

| Protocol | Executable Name |
|----------|-----------------|
| L∞ Fuzzy PSI | `fuzzylinf_bench` |
| L1 Fuzzy PSI | `fuzzyl1_bench` |
| L2 Fuzzy PSI | `fuzzyl2_bench` |

## 2. Catch2 Benchmark Usage

All benchmarks are implemented using the [Catch2](https://github.com/catchorg/Catch2) C++ library. Below are common ways to run and control the benchmarks.

### List All Available Tests

```bash
./fuzzylinf_bench --list-tests
```

### Specify Number of Samples

```bash
# Run each benchmark 3 times
./fuzzylinf_bench --benchmark-samples 3
```

### Run a Specific Test Case

```bash
# Use the test name as an argument to run a specific benchmark
# L∞ 
./fuzzylinf_bench --benchmark-samples 1 "fuzzylinf(n=256 m=256 d=6 delta=10)"

# L1 
./fuzzyl1_bench --benchmark-samples 1 "fuzzyl1(n=4096 m=4096 d=6 delta=10)"
```

### Show Success Details (-s or --success)

By default, Catch2 only displays details for failing tests. Use -s (short for --success) to also show detailed output for successful tests, including benchmark results and SUCCEED() messages:

```bash
# Show detailed output for all tests (including successful ones)
./fuzzylinf_bench -s --benchmark-samples 1 "fuzzylinf(n=256 m=256 d=6 delta=10)"

# Equivalent to above
./fuzzylinf_bench --success --benchmark-samples 1 "fuzzylinf(n=256 m=256 d=6 delta=10)"
```

## 3. Benchmark Collection Script

We provide a convenience script `shell_run_bench.sh` to automatically run both L∞ and L1 Fuzzy PSI benchmarks and save their outputs.

```bash
# usage
./shell_run_bench.sh
```

The script does the following:

1. Runs the L∞ Fuzzy PSI benchmark with 3 samples and detailed output, saving results to `ccs25_balance_linf.log`

2. Runs the L1 Fuzzy PSI benchmark with 3 samples and detailed output, saving results to `ccs25_balance_l1.log`
