FROM ubuntu:22.04

WORKDIR /workspace

ENV DEBIAN_FRONTEND=noninteractive

# Install dependencies
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    cmake \
    autoconf \
    automake \
    libgmp-dev \
    libspdlog-dev \
    libtool \
    libssl-dev \
    libmpfr-dev \
    libfmt-dev \
    nasm \
    python3 \
    python3-pip \
    python3-venv \
    vim \
    git \
    iproute2 \
    net-tools \
    curl \
    wget \
    jq && \
    rm -rf /var/lib/apt/lists/*

# Install third-party dependencies at the revisions pinned by the script.
COPY --chmod=755 ./install-dependencies-in-container.sh /workspace/install-dependencies-in-container.sh
RUN ./install-dependencies-in-container.sh

COPY ./sparseComp /workspace/sparseComp
COPY ./frontend /workspace/frontend
COPY ./tests /workspace/tests
COPY CMakeLists.txt /workspace/CMakeLists.txt

COPY --chmod=755 ./shell_build_cmd.sh /workspace/shell_build_cmd.sh
RUN ./shell_build_cmd.sh

COPY --chmod=755 ./shell_run_bench_fpsi.sh /workspace/shell_run_bench_fpsi.sh
COPY --chmod=755 ./shell_config_network.sh /workspace/shell_config_network.sh
COPY ./README.md /workspace/README.md