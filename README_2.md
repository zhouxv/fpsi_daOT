## run docker

```bash
cmake -S . -B ./build -DCMAKE_BUILD_TYPE=Release -DBUILD_BENCH=ON -DBUILD_TESTS=OFF # -DCMAKE_PREFIX_PATH=../volepsi
cmake --build ./build -j

```

```bash
sudo docker build -t ccs25:latest .

# docker tag ccs25:latest blueobsidian/ccs25:latest
# docker push blueobsidian/ccs25:latest

sudo docker run -dit --name ccs25_100Mbps --cap-add=NET_ADMIN ccs25:latest
```

```
tcset lo --rate 100Mbps --delay 80ms --overwrite
tcset lo --rate 10Mbps --delay 80ms --overwrite
```

```
./fuzzylinf_bench --benchmark-samples 1 -s
nohup ./fuzzylinf_bench --benchmark-samples 1 -s > ccs25-100Mb.log 2>&1 &
```
