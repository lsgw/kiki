#ifndef MESSAGE_H
#define MESSAGE_H

#include <stdlib.h>
#include <stdint.h>
#include <memory>
#include <vector>

#define MSG_TYPE_DEBUGS  1
#define MSG_TYPE_DEBUGR  2
#define MSG_TYPE_EXIT    3
#define MSG_TYPE_TIME    4
#define MSG_TYPE_EVENT   5
#define MSG_TYPE_JSON    6
#define MSG_TYPE_LUA     7
#define MSG_TYPE_LOG     8

struct Message final {
	Message() : type(0) { }
	~Message() {  }
	uint32_t type;
	std::vector<char> data;
};

class Module;
class Context;
using MessagePtr = std::shared_ptr<Message>;
using ModulePtr  = std::shared_ptr<Module>;
using ContextPtr = std::shared_ptr<Context>;

#endif