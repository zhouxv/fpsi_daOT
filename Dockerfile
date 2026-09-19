FROM ubuntu:22.04

WORKDIR /workspace

# Install dependencies
RUN apt-get update && \
    apt-get install -y \
    git \
    python3 \
    python3-pip \
    cmake \
    libgmp-dev \
    libspdlog-dev \
    libtool \
    nasm \
    libssl-dev \
    libmpfr-dev \
    iproute2 \
    net-tools \
    software-properties-common && \
    # install tcconfig for network interface configuration
    pip install tcconfig

RUN apt-get update && \
    apt-get install -y wget

COPY ./install-dependencies-in-container.sh /workspace/install-dependencies-in-container.sh

RUN chmod +x /workspace/install-dependencies-in-container.sh && \
    /workspace/install-dependencies-in-container.sh

COPY ./sparseComp /workspace/sparseComp
COPY ./frontend /workspace/frontend
COPY ./tests /workspace/tests
COPY ./shell_build_cmd.sh /workspace/shell_build_cmd.sh
COPY CMakeLists.txt /workspace/CMakeLists.txt

RUN chmod +x ./shell_build_cmd.sh &&\
    ./shell_build_cmd.sh

COPY ./shell_run_main.sh /workspace/shell_run_main.sh
COPY ./shell_config_network.sh /workspace/shell_config_network.sh
COPY ./README.md /workspace/README.md
RUN chmod +x ./*.sh