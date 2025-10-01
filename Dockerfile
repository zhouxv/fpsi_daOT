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
COPY ./build_bench.sh /home/build_bench.sh
COPY CMakeLists.txt /home/CMakeLists.txt

RUN chmod +x ./build_bench.sh &&\
    ./build_bench.sh && \
    cp ./build/fuzzylinf_bench ./ &&\
    cp ./build/fuzzylinf_16_bench ./ &&\
    cp ./build/fuzzyl1_bench ./ &&\
    cp ./build/fuzzyl1_16_bench ./ &&\
    cp ./build/fuzzyl2_bench ./

COPY ./run_bench.sh /home/run_bench.sh
RUN chmod +x ./run_bench.sh