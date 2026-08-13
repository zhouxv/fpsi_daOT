#!/bin/sh

rm -rf ./build
mkdir -p ./build

# cmake --build ./build/ --target clean

# compile benchmarks
cmake -S . -B ./build -DCMAKE_BUILD_TYPE=Release -DBUILD_BENCH=ON # -DCMAKE_PREFIX_PATH=../volepsi

# compile main executable
# cmake -S . -B ./build -DCMAKE_BUILD_TYPE=Release -DBUILD_BENCH=OFF -DBUILD_MAIN=ON


cmake --build ./build -j
