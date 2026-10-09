# Distance-Aware OT with Application to Fuzzy PSI

This project implements the Fuzzy PSI protocols presented in [Distance-Aware OT with Application to Fuzzy PSI](https://eprint.iacr.org/2025/996).

## Build and Run with Docker

Run the following from this repository's root directory, which contains the
`Dockerfile`. The build installs dependencies and compiles `build/main`;
no prebuilt FPSI image is required.

```bash
docker build -t blueobsidian/fpsi_cmp_artifact:exp12_daot .
docker run -d --cap-add=NET_ADMIN \
  --name fpsi_cmp_exp12 \
  blueobsidian/fpsi_cmp_artifact:exp12_daot \
  sleep infinity
docker exec -it fpsi_cmp_exp12 bash
```

The container's project directory is `/workspace`, not `/home`, and the
executable is `/workspace/build/main`. Run the benchmark commands below
inside this container. `NET_ADMIN` is needed for LAN/WAN network
configuration. Building requires internet access to download dependencies;
pushing an image to a registry is not required.

## Artifact comparison benchmarks (Exp12)

This is the da-ROT-based fuzzy PSI baseline (Exp12) for the comparison
artifact. After building `build/main`, run the following from the project
root. These commands use the artifact driver, not the legacy Catch2
benchmarks below.

```bash
./shell_config_network.sh lan
./shell_run_bench_fpsi.sh
./shell_run_bench_fpsi.sh --preset full
```

Quick is the default and is equivalent to `--preset quick`. The presets
match the camera-ready comparison paper's thresholds:

| Parameter | Quick | Full |
|---|---|---|
| Metrics | Linf, L1, L2 | Linf, L1, L2 |
| Set size N | 2^12 | 2^8, 2^12, 2^16 |
| Dimension d | 2, 6, 10 (L2: 2 only) | 2, 6, 10 (L2: 2 only) |
| Threshold delta | 60, 250 | 60, 250 |
| Trials per combination | 1 | 3 |
| Supported combinations | 14 | 42 |

L2 cases with d > 2 are skipped and produce no CSV rows. Both LAN (10 Gbps,
no added delay) and WAN (100 Mbps, 80 ms target RTT) are paper settings.
Repeat the comparisons under WAN using the same profile for all projects:

```bash
./shell_config_network.sh wan
./shell_run_bench_fpsi.sh --preset quick
./shell_run_bench_fpsi.sh --preset full
```

Use `./shell_run_bench_fpsi.sh --help` and `./shell_config_network.sh --help`
for options. Explicit experiment options override preset values, for example
`./shell_run_bench_fpsi.sh --preset full --nn 12 16`. Network configuration
requires root/sudo locally or `NET_ADMIN` in a container.
