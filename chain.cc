#include "redismodule.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include <string>

// How to use this module:
// Start 3 redis servers with
// redis-server --loadmodule libredis_chain.so --port 6379
// redis-server --loadmodule libredis_chain.so --port 6380
// redis-server --loadmodule libredis_chain.so --port 6381
// And then initialize the module with
// redis-cli -p 6379 chain.initialize head 127.0.0.1 6381 127.0.0.1 6380
// redis-cli -p 6380 chain.initialize head 127.0.0.1 6379 127.0.0.1 6381
// redis-cli -p 6381 chain.initialize head 127.0.0.1 6380 127.0.0.1 6379

class RedisChainModule {
 public:

  enum ChainRole : int {
    HEAD = 0,
    MIDDLE = 1,
    TAIL = 2
  };

  RedisChainModule(const std::string& prev_address, const std::string& prev_port,
                   const std::string& next_address, const std::string& next_port,
                   ChainRole chain_role)
   : prev_address_(prev_address), prev_port_(prev_port),
     next_address_(next_address), next_port_(next_port),
     chain_role_(chain_role), request_id_(0) {}

  int64_t next_request_id() {
    return request_id_++;
  }

  ChainRole chain_role() {
    return chain_role_;
  }
 private:
  std::string prev_address_;
  std::string prev_port_;
  std::string next_address_;
  std::string next_port_;
  ChainRole chain_role_;
  int64_t request_id_;
  // for the protocol, see paper
  // std::set<int64_t> sent_;
  // for implementing flushing
  // std::map<int64_t, RedisString*> key_map_;
};

RedisChainModule* module = nullptr;

std::string ReadString(RedisModuleString *str) {
  size_t l;
  const char* s = RedisModule_StringPtrLen(str, &l);
  return std::string(s, l);
}

// argv[0] is the command name
// argv[1] is the role of this instance ("head", "middle", "tail")
// argv[2] is the address of the previous node in the chain
// argv[3] is the port of the previous node in the chain
// argv[4] is the address of the next node in the chain
// argv[5] is the port of the next node in the chain
int ChainInitialize_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
  // TODO(pcm): Input checking
  REDISMODULE_NOT_USED(argc);
  std::string role = ReadString(argv[1]);
  RedisChainModule::ChainRole chain_role;
  if (role == "head") {
    chain_role = RedisChainModule::ChainRole::HEAD;
  } else if (role == "middle") {
    chain_role = RedisChainModule::ChainRole::MIDDLE;
  } else {
    assert(role == "tail");
    chain_role = RedisChainModule::ChainRole::TAIL;
  }

  // TODO(pcm): Delete this at module shutdown!
  module = new RedisChainModule(ReadString(argv[2]), ReadString(argv[3]),
                                ReadString(argv[4]), ReadString(argv[5]),
                                chain_role);

  // This will set up a chain of slaves
  if (chain_role != RedisChainModule::ChainRole::HEAD) {
    RedisModuleCallReply *rep =
        RedisModule_Call(ctx, "SLAVEOF", "ss", argv[2], argv[3]);
    // TODO(pcm): Error handling
    REDISMODULE_NOT_USED(rep);
  }
  return REDISMODULE_OK;
}

// This is only called on the head node by the client
// argv[0] is the command name
// argv[1] is the key for the data
// argv[2] is the data
int ChainPut_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
  // TODO(pcm): Input checking
  REDISMODULE_NOT_USED(argc);
  long long request_id = module->next_request_id();
  // TODO(pcm): error handling
  RedisModule_Replicate(ctx, "DO_PUT", "ssl", argv[1], argv[2], request_id);
  return REDISMODULE_OK;
}

// argv[0] is the command name
// argv[1] is the key for the data
// argv[2] is the data
// argv[3] is the request ID
int ChainDoPut_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
  // TODO(pcm): Input checking
  REDISMODULE_NOT_USED(argc);
  RedisModuleKey *key;
  key = reinterpret_cast<RedisModuleKey*>(RedisModule_OpenKey(ctx, argv[1], REDISMODULE_WRITE));
  // TODO(pcm): error checking
  RedisModule_StringSet(key, argv[2]);
  long long request_id;
  // TODO(pcm): error checking
  RedisModule_StringToLongLong(argv[1], &request_id);
  if (module->chain_role() != RedisChainModule::TAIL) {
    // report back to client via pubsub
  } else {
    // TODO(pcm): error checking
    RedisModule_Replicate(ctx, "DO_PUT", "ssl", argv[1], argv[2], request_id);
  }
  return REDISMODULE_OK;
}

extern "C" {

int RedisModule_OnLoad(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
  REDISMODULE_NOT_USED(argc);
  REDISMODULE_NOT_USED(argv);
  if (RedisModule_Init(ctx,"chain",1,REDISMODULE_APIVER_1)
      == REDISMODULE_ERR)
    return REDISMODULE_ERR;

  if (RedisModule_CreateCommand(ctx,"chain.initialize",
      ChainInitialize_RedisCommand,"write",1,1,1) == REDISMODULE_ERR)
    return REDISMODULE_ERR;

  if (RedisModule_CreateCommand(ctx,"chain.put",
      ChainPut_RedisCommand,"write",1,1,1) == REDISMODULE_ERR)
    return REDISMODULE_ERR;

  if (RedisModule_CreateCommand(ctx,"chain.do_put",
      ChainDoPut_RedisCommand,"write",1,1,1) == REDISMODULE_ERR)
    return REDISMODULE_ERR;

  return REDISMODULE_OK;
}

}
