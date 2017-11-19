#include "hiredis/hiredis.h"

#include <assert.h>
#include <stdlib.h>

#include <chrono>
#include <iostream>

int main() {
  struct timeval timeout = {1, 500000}; // 1.5 seconds
  redisContext *c = redisConnectWithTimeout("127.0.0.1", 6379, timeout);
  if (c == NULL || c->err) {
    if (c) {
      printf("Connection error: %s\n", c->errstr);
      redisFree(c);
    } else {
      printf("Connection error: can't allocate redis context\n");
    }
    exit(1);
  }

  std::chrono::steady_clock::time_point begin =
      std::chrono::steady_clock::now();
  for (int i = 0; i < 10000; ++i) {
    redisReply *reply = reinterpret_cast<redisReply *>(
        redisCommand(c, "CHAIN.PUT hello world"));
    /*
    if (reply) {
      printf("XXX %s \n", c->errstr);
    } else {
      printf("error\n");
    }
    */
    freeReplyObject(reply);
    if (i % 50 == 0) {
      std::cout << "request " << i << " done\n";
    }
  }
  std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
  std::cout << "took "
            << std::chrono::duration_cast<std::chrono::milliseconds>(end -
                                                                     begin)
                   .count()
            << " millis" << std::endl;
}
