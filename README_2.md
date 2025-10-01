## run docker

```bash
cmake -S . -B ./build -DCMAKE_BUILD_TYPE=Release -DBUILD_BENCH=ON -DBUILD_TESTS=OFF # -DCMAKE_PREFIX_PATH=../volepsi
cmake --build ./build -j

```

```bash
sudo docker build -t ccs25_balance:latest .

# docker tag ccs25_balance:latest blueobsidian/ccs25_balance:latest
# docker push blueobsidian/ccs25_balance:latest

sudo docker run -dit --name ccs25_balance --cap-add=NET_ADMIN ccs25_balance:latest
```

```
tcset lo --rate 100Mbps --delay 80ms --overwrite
tcset lo --rate 10Mbps --delay 80ms --overwrite
```

```
./fuzzylinf_bench --benchmark-samples 1 -s
nohup ./fuzzylinf_bench --benchmark-samples 1 -s > ccs25-100Mb.log 2>&1 &
```
