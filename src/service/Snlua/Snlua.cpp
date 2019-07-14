#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"  
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "Actor.h"
#include "Logger/Log.h"
#include "lua/lua.hpp"
#include <assert.h>
#include <sys/time.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <vector>
#include <set>

using namespace rapidjson;

int ctx_api(lua_State* L);

class Snlua : public Actor<Snlua> {
public:
	void init(ContextPtr ctx, MessagePtr& message) override
	{
		messageref.reset();

		log.ctx    = ctx.get();
		log.handle = ctx->env().logger;

		std::string lua_path   = ctx->env().lua.path;
		std::string lua_cpath  = ctx->env().lua.cpath;
		std::string lua_loader = ctx->env().lua.loader;

		L = luaL_newstate();
		lua_gc(L, LUA_GCSTOP, 0);
		luaL_openlibs(L);
		
		lua_pushlightuserdata(L, ctx.get());
		lua_setfield(L, LUA_REGISTRYINDEX, "context");
		luaL_requiref(L, "ctx", ctx_api, 1);

		lua_pushlstring(L, lua_path.data(), lua_path.size());
		lua_setglobal(L, "LUA_PATH");

		lua_pushlstring(L, lua_cpath.data(), lua_cpath.size());
		lua_setglobal(L, "LUA_CPATH");

		lua_sethook(L, Snlua::hook, LUA_MASKCOUNT, 4000);

		int r = luaL_loadfile(L, lua_loader.c_str());
		if (r != LUA_OK) {
			LOG_INFO << "Can't load " << lua_loader << " : " << lua_tostring(L, -1);
			ctx->exit();
			return;
		}

		auto& data = message->data;
		lua_pushlstring(L, static_cast<char*>(&*data.begin()), data.size());
		r = lua_resume(L, NULL, 1);
		if (r == 0) {
			LOG_ERROR << "init to exit";
			ctx->exit();
			return;
		}
		if (r != LUA_YIELD) {
			LOG_ERROR << "lua loader error:\n" << lua_tostring(L, -1);
			ctx->exit();
			return;
		}
		bool match = true;
		int top = lua_gettop(L);
		if (top == 0) {
			//printf("[%010d] init receive hook lua_yield = %d\n", ctx->handle(), (int)match);
		} else {
			assert(top == 1);
			assert(lua_isboolean(L, 1));
			match = lua_toboolean(L, 1);
			assert(match == true);
			//printf("[%010d] init receive main lua_yield = %d\n", ctx->handle(), (int)match);
		}
		
		lua_sethook(L, NULL, 0, 0);

		lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_MAINTHREAD);
		lua_State *gL = lua_tothread(L,-1);
		assert(L == gL);

		lua_settop(L, 0);
		lua_gc(L, LUA_GCRESTART, 0);
	}
	void release(ContextPtr ctx, MessagePtr& message) override
	{
		Document document;
		Document::AllocatorType& allocator = document.GetAllocator();
		Value array(kArrayType);
		array.PushBack("cast", allocator);
		array.PushBack(ctx->handle(), allocator);
		array.PushBack("ref",  allocator);
		array.PushBack("exit", allocator);
		array.PushBack(ctx->handle(), allocator);

		StringBuffer buffer;
		Writer<rapidjson::StringBuffer> writer(buffer);
		array.Accept(writer);
		std::string args = buffer.GetString();
		ctx->send(ctx->env().launcher, ctx->makeMessage(MSG_TYPE_JSON, std::vector<char>(args.begin(), args.end())));
		if (ctx->loop() != NULL) {
			for (int fd : fds) {
				ctx->channel(fd)->disableAll()->update();
				::close(fd);
			}
		}
		lua_close(L);
	}

	bool receive(ContextPtr ctx, MessagePtr& message) override
	{
		// printf("[:%08x] source(%08x) type(%d) data(%p) size(%d) pending = %u\n", ctx->handle(), message->source, message->type, message->data, message->size, ctx->mailboxLength());
		
		lua_sethook(L, Snlua::hook, hookMask, hookCount);

		if (message->type == MSG_TYPE_DEBUGS) { // 此消息必须一次性处理完成
			//debug(ctx, static_cast<const char*>(message->data), message->size);
			return true;
		}
		if (debugBreakpoint) {
			return false;
		}

		int r = 0;
		if (message->type == 0) {
			r = lua_resume(L, NULL, 0);
			endlessLoopCount++;
		} else {
			messageref = message;
			lua_pushlightuserdata(L, messageref.get());
			r = lua_resume(L, NULL, 1);
			endlessLoopCount = 0;
		}


		if (r == 0) {
			ctx->exit();
			return true;
		}
		if (r != LUA_YIELD) {
			std::string callstack;
			int level = 0;
			lua_Debug ar;
			while (lua_getstack(L, level, &ar)) {
				lua_getinfo(L, "nlS", &ar);
				std::string filename = ar.source? ar.source : "null";
				std::string funcname = ar.name? ar.name : "null" ;
				std::string fileline = std::to_string(ar.currentline);
				callstack.append(std::to_string(level) + "  " + filename + "  " + funcname + "  " + fileline + "\n");
				level++;
			}
			LOG_ERROR << "[lua vm error] " << lua_tostring(L, -1) << "\n" << callstack;
			ctx->exit();
			return true;
		}


		bool match = true;
		int top = lua_gettop(L);
		if (top == 0) {
			// printf("[:%08x] receive hook lua_yield = %d\n", ctx->handle(), (int)match);
		} else {
			// printf("[:%08x] receive main lua_yield = %d\n", ctx->handle(), (int)match);
			assert(top == 1);
			assert(lua_isboolean(L, 1));
			match = lua_toboolean(L, 1);
			if (match) {
				messageref.reset();
			}
		}
		if (endlessLoopCount > 0) {
			LOG_WARN << "maybe in an endless loop";
			endlessLoopCount = 0;
		}
		lua_settop(L, 0);
		lua_sethook(L, 0, 0, 0);
		return match;
	}
	bool hasDebugBreakpoint(Context* ctx, lua_Debug* ar)
	{
		return false;
	}

	MessagePtr messageref;
	std::set<int> fds;
private:
	static void hook(lua_State* L, lua_Debug* ar)
	{
		if (lua_isyieldable(L)) {
			lua_getfield(L, LUA_REGISTRYINDEX, "context");
			Context* ctx = (Context*)lua_touserdata(L, -1);
			Snlua* self = (Snlua*)ctx->actor();
			lua_getinfo(L, "lS", ar);
			if (self->hookCount == kDebugHookCount && self->hasDebugBreakpoint(ctx, ar)) {	
				self->debugBreakpoint = 1;
			} else {
				ctx->yield();
			}
			lua_yield(L, 0);
		}
	}

	static const int kNormalHookMask  = LUA_MASKCOUNT;  // 正常调度模式
	static const int kNormalHookCount = 1000;           // 正常调度模式

	static const int kDebugHookMask   = LUA_MASKLINE;   // 调试调度模式
	static const int kDebugHookCount  = 0;              // 调试调度模式
	
	static const int kNext = 0;
	static const int kStep = 1;

	lua_State* L;
	int hookCount = kNormalHookCount;
	int hookMask  = kNormalHookMask;

	int debugBreakpoint = 0;
	int debugActionType = kNext;
	
	int endlessLoopCount = 0;
	Log log;
};

static int lua_env(lua_State* L)
{
	Context* ctx = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	assert(ctx != NULL);
	lua_newtable(L);               // env


	lua_pushstring(L, "thread");   // env.thread
	lua_newtable(L);

	lua_pushstring(L, "event");    // env.thread.event
	lua_pushinteger(L, ctx->env().thread.event);
	lua_settable(L, -3);

	lua_pushstring(L, "worker");   // env.thread.worker
	lua_pushinteger(L, ctx->env().thread.worker);
	lua_settable(L, -3);

	lua_settable(L, -3);

	//--------------//

	lua_pushstring(L, "module");   // env.module
	lua_newtable(L);
	
	lua_pushstring(L, "path");     // env.module.path
	lua_newtable(L);
	for (int i=0; i<ctx->env().module.path.size(); i++) {
		lua_pushinteger(L, i+1);
		lua_pushlstring(L, ctx->env().module.path[i].data(), ctx->env().module.path[i].size());
		lua_settable(L, -3);
	}
	lua_settable(L, -3);

	lua_settable(L, -3);

	//--------------//

	lua_pushstring(L, "lua");      // env.lua
	lua_newtable(L);

	lua_pushstring(L, "path");     // env.lua.path
	lua_pushlstring(L, ctx->env().lua.path.data(), ctx->env().lua.path.size());
	lua_settable(L, -3);

	lua_pushstring(L, "cpath");    // env.lua.cpath
	lua_pushlstring(L, ctx->env().lua.cpath.data(), ctx->env().lua.cpath.size());
	lua_settable(L, -3);

	lua_pushstring(L, "loader");   // env.lua.loader
	lua_pushlstring(L, ctx->env().lua.loader.data(), ctx->env().lua.loader.size());
	lua_settable(L, -3);

	lua_settable(L, -3);

	//--------------//

	lua_pushstring(L, "entry");    // env.entry
	lua_newtable(L);

	lua_pushstring(L, "main");     // env.entry.main
	lua_pushlstring(L, ctx->env().entry.main.data(), ctx->env().entry.main.size());
	lua_settable(L, -3);

	lua_pushstring(L, "args");     // env.entry.args
	lua_pushlstring(L, ctx->env().entry.args.data(), ctx->env().entry.args.size());
	lua_settable(L, -3);

	lua_settable(L, -3);

	//--------------//

	lua_pushstring(L, "daemon");   // env.daemon
	lua_pushboolean(L, ctx->env().daemon);
	lua_settable(L, -3);

	//--------------//

	lua_pushstring(L, "profile");  // env.profile
	lua_pushboolean(L, ctx->env().profile);
	lua_settable(L, -3);
	
	//--------------//

	lua_pushstring(L, "launcher"); // env.launcher
	lua_pushinteger(L, ctx->env().launcher);
	lua_settable(L, -3);

	//--------------//

	lua_pushstring(L, "logger"); // env.logger
	lua_pushinteger(L, ctx->env().logger);
	lua_settable(L, -3);

	//--------------//

	lua_pushstring(L, "signal"); // env.signal
	lua_pushinteger(L, ctx->env().signal);
	lua_settable(L, -3);

	return 1;
}
static int lua_handle(lua_State* L)
{
	Context* ctx = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	assert(ctx != NULL);
	lua_pushinteger(L, ctx->handle());
	return 1;
}
static int lua_owner(lua_State* L)
{
	Context* ctx = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	assert(ctx != NULL);
	lua_pushinteger(L, ctx->owner());
	return 1;
}
static int lua_setowner(lua_State* L)
{
	assert(lua_gettop(L) == 1);
	Context* ctx = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	assert(ctx != NULL);
	uint32_t oldowner = ctx->owner();
	uint32_t newowner = (uint32_t)luaL_checkinteger(L, 1);
	ctx->setOwner(newowner);
	lua_pushinteger(L, oldowner);
	return 1;
}
static int lua_newsession(lua_State* L)
{
	Context* ctx = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	assert(ctx != NULL);
	lua_pushinteger(L, ctx->newsession());
	return 1;
}
static int lua_send(lua_State* L)
{
	assert(lua_gettop(L) == 3);
	Context* ctx = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	assert(ctx != NULL);

	size_t   len    = 0;
	uint32_t handle = (uint32_t)luaL_checkinteger(L, 1);
	uint32_t type   = (uint32_t)luaL_checkinteger(L, 2);
	char*    str    = (char*)luaL_checklstring(L, 3, &len);
	
	std::vector<char> v(len);
	std::copy(str, str+len, &*v.begin());
	auto msg     = ctx->makeMessage(type, std::move(v));

	bool b = ctx->send(handle, msg);
	if (b) {
		lua_pushboolean(L, 1);
		lua_pushnil(L);
	} else {
		std::string err = "send to invalid address: " + std::to_string(handle);
		lua_pushboolean(L, 0);
		lua_pushlstring(L, err.data(), err.size());
	}
	return 2;
}
static int lua_timeout(lua_State* L)
{
	assert(lua_gettop(L) == 2);
	Context* ctx = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	assert(ctx != NULL);
	uint32_t session = (uint32_t)luaL_checkinteger(L, 1);
	uint64_t ms      = (uint64_t)luaL_checkinteger(L, 2);
	ctx->timeout(session, ms);
	return 0;
}
static int lua_reload(lua_State* L)
{
	assert(lua_gettop(L) == 1);
	Context* ctx = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	assert(ctx != NULL);
	
	size_t len    = 0;
	char*  module = (char*)luaL_checklstring(L, 1, &len);
	ctx->reload(std::string(module, len));

	return 0;
}

static int lua_exit(lua_State* L)
{
	assert(lua_gettop(L) == 0);
	Context* ctx = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	assert(ctx != NULL);
	ctx->exit();
	return 0;
}
static int lua_abort(lua_State* L)
{
	assert(lua_gettop(L) == 0);
	Context* ctx = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	assert(ctx != NULL);
	ctx->abort();
	return 0;
}
static int lua_rlen(lua_State* L)
{
	assert(lua_gettop(L) == 0);
	Context* ctx = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	assert(ctx != NULL);
	lua_pushinteger(L, ctx->rlen());
	return 1;
}
static int lua_qlen(lua_State* L)
{
	assert(lua_gettop(L) == 0);
	Context* ctx = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	assert(ctx != NULL);
	lua_pushinteger(L, ctx->qlen());
	return 1;
}

static int lua_mlen(lua_State* L)
{
	assert(lua_gettop(L) == 0);
	Context* ctx = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	assert(ctx != NULL);
	lua_pushinteger(L, ctx->mlen());
	return 1;
}
static int lua_cpucost(lua_State* L)
{
	assert(lua_gettop(L) == 0);
	Context* ctx = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	assert(ctx != NULL);
	lua_pushinteger(L, ctx->cpuCost());
	return 1;
}
static int lua_recvcount(lua_State* L)
{
	assert(lua_gettop(L) == 0);
	Context* ctx = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	assert(ctx != NULL);
	lua_pushinteger(L, ctx->messageCount());
	return 1;
}
static int lua_profile(lua_State* L)
{
	assert(lua_gettop(L) == 0);
	Context* ctx = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	assert(ctx != NULL);
	lua_pushboolean(L, ctx->getProfile());
	return 1;
}
static int lua_now(lua_State* L)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	uint64_t cp = (uint64_t)tv.tv_sec * 1000000 + (uint64_t)tv.tv_usec;
	lua_pushinteger(L, cp);
	return 1;
}
static int lua_unpack(lua_State* L)
{
	assert(lua_gettop(L) == 1);
	assert(lua_type(L, 1) == LUA_TLIGHTUSERDATA);
	Context* ctx = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	assert(ctx != NULL);
	Snlua* self = (Snlua*)ctx->actor();
	void* ptr   = (void*)lua_touserdata(L, 1);
	assert(ptr == self->messageref.get());

	char*    udata = &*(self->messageref->data.begin());
	uint32_t nbyte = self->messageref->data.size();

	lua_pushinteger(L,       self->messageref->type);
	lua_pushlightuserdata(L, udata);
	lua_pushinteger(L,       nbyte);

	return 3;
}
static int lua_evregister(lua_State* L)
{
	assert(lua_gettop(L) == 2);
	assert(lua_type(L, 2) == LUA_TTABLE);
	Context* ctx = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	assert(ctx != NULL);
	assert(ctx->loop() != NULL);

	bool ev = false;
	int  fd = (int)luaL_checkinteger(L, 1);
	auto ch = ctx->channel(fd);
	assert(fd > 0);
	
	lua_pushnil(L);
	while(lua_next(L, -2)) {
		if (lua_type(L, -2)==LUA_TSTRING && (lua_type(L, -1)==LUA_TBOOLEAN)) {
			size_t nkey = 0;
			const char* key = luaL_checklstring(L, -2, &nkey);
			if (strncmp(key, "read", nkey) == 0) {
				if (lua_toboolean(L, -1)) {
					ch->enableReading();
				} else {
					ch->disableReading();
				}
				ev = true;
			}
			if (strncmp(key, "write", nkey) == 0) {
				if(lua_toboolean(L, -1)) {
					ch->enableWriting();
				} else {
					ch->disableWriting();
				}
				ev = true;
			}
		}
		lua_pop(L, 1);
	}
	if (ev) {
		ch->update();
	}
	return 0;
}
static int lua_bind(lua_State* L)
{
	assert(lua_gettop(L) == 1);
	int fd = (int)luaL_checkinteger(L, 1);
	assert(fd >= 0);
	Context* ctx = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	assert(ctx != NULL);
	Snlua* self = (Snlua*)ctx->actor();
	self->fds.insert(fd);
	return 0;
}
static int lua_unbind(lua_State* L)
{
	assert(lua_gettop(L) == 1);
	int fd = (int)luaL_checkinteger(L, 1);
	assert(fd >= 0);
	Context* ctx = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	assert(ctx != NULL);
	Snlua* self = (Snlua*)ctx->actor();
	assert(self->fds.find(fd) != self->fds.end());
	self->fds.erase(fd);
	return 0;
}

static int lua_protocol(lua_State* L)
{
	uint32_t protoid = 0;
	size_t len = 0;
	const char* proto = luaL_checklstring(L, 1, &len);
	if (strncmp("debugs", proto, len) == 0) {
		protoid = MSG_TYPE_DEBUGS;
	} else if (strncmp("debugr", proto, len) == 0) {
		protoid = MSG_TYPE_DEBUGR;
	} else if (strncmp("exit", proto, len) == 0) {
		protoid = MSG_TYPE_EXIT;
	} else if (strncmp("time", proto, len) == 0) {
		protoid = MSG_TYPE_TIME;
	} else if (strncmp("event", proto, len) == 0) {
		protoid = MSG_TYPE_EVENT;
	} else if (strncmp("json", proto, len) == 0) {
		protoid = MSG_TYPE_JSON;
	} else if (strncmp("lua", proto, len) == 0) {
		protoid = MSG_TYPE_LUA;
	} else if (strncmp("log", proto, len) == 0) {
		protoid = MSG_TYPE_LOG;
	}
	if (protoid >= 0) {
		lua_pushinteger(L, protoid);
	} else {
		lua_pushnil(L);
	}
	
	return 1;
}

static int lua_decode_timeout(lua_State* L)
{
	assert(lua_gettop(L) == 2);
	assert(lua_type(L,1) == LUA_TLIGHTUSERDATA);
	Context* ctx = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	assert(ctx != NULL);
	void*    udata   = (void*)lua_touserdata(L, 1);
	uint32_t nbyte   = (uint32_t)luaL_checkinteger(L, 2);
	uint32_t type    = 0;
	uint32_t handle  = 0;
	uint32_t session = 0;
	uint64_t time    = 0;

	assert(nbyte == (sizeof(type)+sizeof(handle)+sizeof(session)+sizeof(time)));
	char* p = (char*)udata;
	::memcpy(&type, p, sizeof(type));
	p += sizeof(type);
	::memcpy(&handle, p, sizeof(handle));
	p += sizeof(handle);
	::memcpy(&session, p, sizeof(session));
	p += sizeof(session);
	::memcpy(&time, p, sizeof(time));

	assert(ctx->handle() == handle);
	assert((type==0 && ctx->loop()==NULL) || (type==1 && ctx->loop()!=NULL));

	lua_pushinteger(L, session);
	lua_pushinteger(L, time);
	
	return 2;
}

#define IOREAD  1
#define IOWRITE 2
#define IOERROR 3
static int lua_decode_ioevent(lua_State* L)
{
	assert(lua_gettop(L) == 2);
	assert(lua_type(L,1) == LUA_TLIGHTUSERDATA);
	Context* ctx = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	assert(ctx != NULL);
	void*    udata    = (void*)lua_touserdata(L, 1);
	uint32_t nbyte    = (uint32_t)luaL_checkinteger(L, 2);
	uint64_t eventtime =  0;
	uint32_t eventtype =  0;
	int      eventfd   = -1;
	
	assert(nbyte == (sizeof(eventtime)+sizeof(eventtype)+sizeof(eventfd)));
	char* p = (char*)udata;
	::memcpy(&eventtime, p, sizeof(eventtime));
	p += sizeof(eventtime);
	::memcpy(&eventtype, p, sizeof(eventtype));
	p += sizeof(eventtype);
	::memcpy(&eventfd,   p, sizeof(eventfd));

	assert(eventfd >= 0);
	
	lua_pushinteger(L, eventtime);
	if (eventtype == IOREAD) {
		lua_pushstring(L, "read");
	} else if (eventtype == IOWRITE) {
		lua_pushstring(L, "write");
	} else {
		assert(eventtype == IOERROR);
		lua_pushstring(L, "error");
	}
	lua_pushinteger(L, eventfd);
	
	return 3;
}
static int lua_encode_logger(lua_State* L)
{
	assert(lua_gettop(L) == 2);
	assert(lua_type(L, 2) == LUA_TSTRING);
	Context* ctx = (Context*)lua_touserdata(L, lua_upvalueindex(1));
	assert(ctx != NULL);

	uint32_t level = (uint32_t)luaL_checkinteger(L, 1);
	size_t len  = 0;
	char*  str  = (char*)lua_tolstring(L, 2, &len);

	uint32_t source = ctx->handle();
	std::vector<char> v(sizeof(source) + sizeof(level) + len + 1);
	char* p = &*v.begin();
	::memcpy(p, &source, sizeof(source));
	p += sizeof(source);
	::memcpy(p, &level, sizeof(level));
	p += sizeof(level);
	snprintf(p, len + 1, "%.*s", static_cast<int>(len), str);

	lua_pushlstring(L, &*v.begin(), v.size());

	return 1;
}



int ctx_api(lua_State* L)
{
	luaL_checkversion(L);
	
	luaL_Reg funcs[] = {
		{ "env",            lua_env            },
	    { "handle",         lua_handle         },
		{ "owner",          lua_owner          },
		{ "setowner",       lua_setowner       },
		{ "newsession",     lua_newsession     },
		{ "send",           lua_send           },
		{ "timeout",        lua_timeout        },
		{ "reload",         lua_reload         },
		{ "exit",           lua_exit           },
		{ "abort",          lua_abort          },
		{ "rlen",           lua_rlen           },
		{ "qlen",           lua_qlen           },
		{ "mlen",           lua_mlen           },
		{ "cpucost",        lua_cpucost        },
		{ "recvcount",      lua_recvcount      },
		{ "profile",        lua_profile        },
		{ "now",            lua_now            },
		{ "unpack",         lua_unpack         },
		{ "register",       lua_evregister     },
		{ "bind",           lua_bind           },
		{ "unbind",         lua_unbind         },
		{ "protocol",       lua_protocol       },
		{ "decode_timeout", lua_decode_timeout },
		{ "decode_ioevent", lua_decode_ioevent },
		{ "encode_logger",  lua_encode_logger  },
	    { NULL,             NULL               }
	};

	luaL_newlibtable(L, funcs);
	lua_getfield(L, LUA_REGISTRYINDEX, "context");
	Context* ctx = (Context*)lua_touserdata(L,-1);
	if (ctx == NULL) {
		return luaL_error(L, "Init context first");
	}
	luaL_setfuncs(L, funcs, 1);

	return 1;
}

module(Snlua)