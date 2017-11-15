#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

extern "C" {
#include "redismodule.h"
#include "hiredis/hiredis.h"
#include "hiredis/async.h"
#include "hiredis/adapters/ae.h"
}

extern "C" {
#include "redis/src/ae.h"
}

#include <iostream>
#include <string>
#include <vector>

extern "C" {
aeEventLoop *getEventLoop();
}

// How to use this module:
// Start 3 redis servers with
// redis-server --loadmodule libredis_chain.so --port 6379
// redis-server --loadmodule libredis_chain.so --port 6380
// redis-server --loadmodule libredis_chain.so --port 6381
// And then initialize the module with
// redis-cli -p 6379 chain.initialize head 127.0.0.1 6381 127.0.0.1 6380
// redis-cli -p 6380 chain.initialize head 127.0.0.1 6379 127.0.0.1 6381
// redis-cli -p 6381 chain.initialize head 127.0.0.1 6380 127.0.0.1 6379

class RedisCommandBuilder {
 public:
  RedisCommandBuilder(int64_t num_args, const std::string& command) {
    std::string prefix = "*" + std::to_string(num_args + 1) + "\r\n$" + std::to_string(command.size()) + "\r\n" + command + "\r\n";
    std::copy(prefix.begin(), prefix.end(), std::back_inserter(command_));
    prefix_length_ = command_.size();
  }
  void Reset() {
    command_.resize(prefix_length_);
  }
  void AppendArg(const char* data, int64_t length) {
    command_.push_back('$');
    int64_t n = length;
    do {
      command_.push_back(n % 10 + '0');
    } while ((n /= 10) > 0);
    command_.push_back('\r');
    command_.push_back('\n');
    for (int64_t i = 0; i < length; ++i) {
      command_.push_back(data[i]);
    }
    command_.push_back('\r');
    command_.push_back('\n');
  }
  const char* data() {
    return command_.data();
  }
  size_t size() {
    return command_.size();
  }
private:
  int64_t prefix_length_;
  std::vector<char> command_;
};

class RedisChainModule {
 public:

  enum ChainRole : int {
    HEAD = 0,
    MIDDLE = 1,
    TAIL = 2
  };

  RedisChainModule(const std::string& prev_address, const std::string& prev_port,
                   const std::string& next_address, const std::string& next_port,
                   ChainRole chain_role, redisAsyncContext* child)
   : prev_address_(prev_address), prev_port_(prev_port),
     next_address_(next_address), next_port_(next_port),
     chain_role_(chain_role), request_id_(0), child_(child), builder_(3, "CHAIN.DO_PUT") {}

  int64_t next_request_id() {
    return request_id_++;
  }

  ChainRole chain_role() {
    return chain_role_;
  }

  redisAsyncContext* child() {
    return child_;
  }

  RedisCommandBuilder& builder() {
    return builder_;
  }

 private:
  std::string prev_address_;
  std::string prev_port_;
  std::string next_address_;
  std::string next_port_;
  ChainRole chain_role_;
  int64_t request_id_;
  RedisCommandBuilder builder_;
  // TODO(pcm): close this at shutdown
  redisAsyncContext* child_;

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
  REDISMODULE_NOT_USED(ctx);
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

  std::string prev_address = ReadString(argv[2]);
  std::string prev_port = ReadString(argv[3]);
  std::string next_address = ReadString(argv[4]);
  std::string next_port = ReadString(argv[5]);

  redisAsyncContext *c = redisAsyncConnect(next_address.c_str(), std::stoi(next_port));
  if (c == NULL || c->err) {
    if (c) {
      printf("Connection error: %s\n", c->errstr);
      redisAsyncFree(c);
    } else {
      printf("Connection error: can't allocate redis context\n");
    }
    exit(1);
  }

  aeEventLoop* loop = getEventLoop();
  redisAeAttach(loop, c);

  // TODO(pcm): Delete this at module shutdown!
  module = new RedisChainModule(prev_address, prev_port,
                                next_address, next_port,
                                chain_role, c);

  RedisModule_ReplyWithNull(ctx);
  return REDISMODULE_OK;
}

int Put(RedisModuleCtx *ctx, RedisModuleString *name, RedisModuleString* data, long long request_id) {
  RedisModuleKey *key;
  key = reinterpret_cast<RedisModuleKey*>(RedisModule_OpenKey(ctx, name, REDISMODULE_WRITE));
  // TODO(pcm): error checking
  RedisModule_StringSet(key, data);
  if (module->chain_role() == RedisChainModule::TAIL) {
    // report back to client via pubsub
  } else {
    std::string key = ReadString(name);
    std::string val = ReadString(data);
    std::string rid = std::to_string(request_id);
    module->builder().Reset();
    module->builder().AppendArg(key.data(), key.size());
    module->builder().AppendArg(val.data(), val.size());
    module->builder().AppendArg(rid.data(), rid.size());
    // redisReply* reply = reinterpret_cast<redisReply*>(redisAsyncCommand(module->child(), NULL, NULL, "CHAIN.DO_PUT %b %b %b", key.data(), key.size(), val.data(), val.size(), rid.data(), rid.size()));
    // TODO(pcm): print this stuff
    redisReply* reply = reinterpret_cast<redisReply*>(redisAsyncFormattedCommand(module->child(), NULL, NULL, module->builder().data(), module->builder().size()));
    // assert(reply == NULL);
    freeReplyObject(reply);
  }
  RedisModule_ReplyWithNull(ctx);
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
  return Put(ctx, argv[1], argv[2], request_id);
}

// argv[0] is the command name
// argv[1] is the key for the data
// argv[2] is the data
// argv[3] is the request ID
int ChainDoPut_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
  // TODO(pcm): Input checking
  REDISMODULE_NOT_USED(argc);
  long long request_id;
  RedisModule_StringToLongLong(argv[1], &request_id);
  return Put(ctx, argv[1], argv[2], request_id);
}

extern "C" {

int RedisModule_OnLoad(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
  REDISMODULE_NOT_USED(argc);
  REDISMODULE_NOT_USED(argv);
  if (RedisModule_Init(ctx,"chain",1,REDISMODULE_APIVER_1)
      == REDISMODULE_ERR)
    return REDISMODULE_ERR;

  if (RedisModule_CreateCommand(ctx,"CHAIN.INITIALIZE",
      ChainInitialize_RedisCommand,"write",1,1,1) == REDISMODULE_ERR)
    return REDISMODULE_ERR;

  if (RedisModule_CreateCommand(ctx,"CHAIN.PUT",
      ChainPut_RedisCommand,"write",1,1,1) == REDISMODULE_ERR)
    return REDISMODULE_ERR;

  if (RedisModule_CreateCommand(ctx,"CHAIN.DO_PUT",
      ChainDoPut_RedisCommand,"write",1,1,1) == REDISMODULE_ERR)
    return REDISMODULE_ERR;

  return REDISMODULE_OK;
}

}
