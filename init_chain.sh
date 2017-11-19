#!/bin/bash
set -ex

make clean
make -j

./redis/src/redis-server --loadmodule libredis_chain_async.so --port 6379 &> 6379.log &
./redis/src/redis-server --loadmodule libredis_chain_async.so --port 6380 &> 6380.log &
./redis/src/redis-server --loadmodule libredis_chain_async.so --port 6381 &> 6381.log &

sleep 3

./redis/src/redis-cli -p 6379 chain.initialize head 0.0.0.0 6381 0.0.0.0 6380
./redis/src/redis-cli -p 6380 chain.initialize middle 0.0.0.0 6379 0.0.0.0 6381
./redis/src/redis-cli -p 6381 chain.initialize tail 0.0.0.0 6380 0.0.0.0 6379
