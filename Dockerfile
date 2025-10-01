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
COPY ./build-bench.sh /home/build-bench.sh
COPY ./build-rls.sh /home/build-rls.sh
COPY ./build-tests.sh /home/build-tests.sh
COPY CMakeLists.txt /home/CMakeLists.txt

RUN chmod +x ./*.sh &&\
    ./build-bench.sh && \
    cp ./build/fuzzylinf_bench ./ &&\
    cp ./build/fuzzyl1_bench ./ &&\
    cp ./build/fuzzyl2_bench ./
