FROM ubuntu:22.04

WORKDIR /home

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

COPY ./install-dependencies-in-container.sh /home/install-dependencies-in-container.sh

RUN chmod +x /home/install-dependencies-in-container.sh && \
    /home/install-dependencies-in-container.sh

COPY ./sparseComp /home/sparseComp
COPY ./tests /home/tests
COPY ./shell_build_cmd.sh /home/shell_build_cmd.sh
COPY CMakeLists.txt /home/CMakeLists.txt

RUN chmod +x ./shell_build_cmd.sh &&\
    ./shell_build_cmd.sh && \
    cp ./build/fuzzylinf_bench ./ &&\
    cp ./build/fuzzyl1_bench ./ &&\
    cp ./build/fuzzyl2_bench ./

COPY ./shell_run_bench.sh /home/shell_run_bench.sh
COPY ./shell_run_main.sh /home/shell_run_main.sh
COPY ./README.md /home/README.md
RUN chmod +x ./*.sh