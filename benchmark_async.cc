#include <assert.h>
#include <stdlib.h>

extern "C" {
#include "hiredis/hiredis.h"
#include "hiredis/async.h"
#include "hiredis/adapters/ae.h"

#include <ae.h>
}

#include <chrono>
#include <iostream>
#include <vector>

static aeEventLoop *loop;
redisAsyncContext *client;

int64_t now() {
  return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

int64_t T = 0;

std::vector<int64_t> timestamps1;
std::vector<int64_t> timestamps2;

void Callback(redisAsyncContext* c, void* r, void* privdata) {
  if (r == NULL) {
    return;
  }
  int64_t t = reinterpret_cast<int64_t>(privdata);
  timestamps1.push_back(now() - T);
  /*
  if(timestamps1.size() == 3) {
    aeStop(loop);
  }
  */
}

void SubscriptionCallback(redisAsyncContext* c, void* r, void* privdata) {
  if (r == NULL) {
    return;
  }
  int64_t t = reinterpret_cast<int64_t>(privdata);
  timestamps2.push_back(now() - t);
  if (timestamps2.size() == 1) {
    T = now();
    for (int i = 0; i < 3; ++i) {
      redisAsyncCommand(client, Callback, reinterpret_cast<void*>(T), "CHAIN.PUT hello world");
    }
  }
  if(timestamps2.size() == 3) {
    aeStop(loop);
  }
}

void connectCallback(const redisAsyncContext *c, int status) {
  printf("connected\n");
  T = now();
}

int main() {
  // need to set CONFIG SET protected-mode no
  client = redisAsyncConnect("172.31.18.230", 6379);
  if (client->err) {
    /* Let *c leak for now... */
    printf("Error: %s\n", client->errstr);
    return 1;
  }
  // IMPORTANT: need a separate context for subscribers
  redisAsyncContext *s = redisAsyncConnect("172.31.19.175", 6379);
  if (s->err) {
    /* Let *c leak for now... */
    printf("Error: %s\n", s->errstr);
    return 1;
  }
  loop = aeCreateEventLoop(64);
  redisAeAttach(loop, client);
  redisAsyncSetConnectCallback(client,connectCallback);
  redisAeAttach(loop, s);
  redisAsyncCommand(s, SubscriptionCallback, reinterpret_cast<void*>(now()), "SUBSCRIBE answers");
  aeMain(loop);
  for (const int64_t& t : timestamps1) {
    std::cout << "time1: " << t << std::endl;
  }
  for (const int64_t& t : timestamps2) {
    std::cout << "time2: " << t << std::endl;
  }
}
