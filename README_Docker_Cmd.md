## Build the executable file

```bash
cmake -S . -B ./build -DCMAKE_BUILD_TYPE=Release -DBUILD_BENCH=ON -DBUILD_TESTS=OFF # -DCMAKE_PREFIX_PATH=../volepsi
cmake --build ./build -j

```

## build docker image

```bash
sudo docker build -t blueobsidian/fpsi_cmp_artifact:exp12_daot .

docker push blueobsidian/fpsi_cmp_artifact:exp12_daot

sudo docker run -dit --name fpsi_daot --cap-add=NET_ADMIN blueobsidian/fpsi_cmp_artifact:exp12_daot
```

## Network Traffic Control

```
tcset lo --rate 100Mbps --delay 80ms --overwrite
tcset lo --rate 10Mbps --delay 80ms --overwrite
```

## run bench

```
./fuzzylinf_bench --benchmark-samples 1 -s
nohup ./run_bench.sh > log 2>&1 &
```
