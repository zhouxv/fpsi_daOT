# Fuzzy PSI

This project implements the Fuzzy PSI protocols presented in [Distance-Aware OT with Application to Fuzzy PSI](https://eprint.iacr.org/2025/996).

## Legacy Catch2 Benchmarks

When built, the Catch2 executables are located in the project's `build` directory. The following benchmark executables are available:

| Protocol | Executable Name |
|----------|-----------------|
| L∞ Fuzzy PSI | `fuzzylinf_bench` |
| L1 Fuzzy PSI | `fuzzyl1_bench` |
| L2 Fuzzy PSI | `fuzzyl2_bench` |

### Catch2 Usage

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
