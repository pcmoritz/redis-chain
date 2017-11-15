# redis-chain
```bash
git submodule init
git submodule update

pushd hiredis && make -j && popd
pushd redis && make -j && popd

cmake .
make -j

./init_chain.sh
./benchmark
```
