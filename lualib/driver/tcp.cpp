#include "lua/lua.hpp"
#include "sockets.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>

static int lua_tcp_listen(lua_State* L)
{
	assert(lua_gettop(L) == 1);
	assert(lua_type(L, 1) == LUA_TTABLE);

	char     ip[64] = { "0.0.0.0" };
	uint16_t port   = 0;
	bool     ipv6   = false;

	bool reuseaddr  = false;
	bool reuseport  = false;
	
	lua_pushnil(L);
	while(lua_next(L, -2)) {
		assert(lua_type(L, -2) == LUA_TSTRING);
		size_t nkey = 0;
		const char* key = luaL_checklstring(L, -2, &nkey);
		if (strncmp(key, "ip", nkey) == 0) {
			size_t nvalue = 0;
			const char* value = luaL_checklstring(L, -1, &nvalue);
			strncpy(ip, value, nvalue);
		} else if (strncmp(key, "port", nkey) == 0) {
			port = (uint16_t)luaL_checkinteger(L, -1);
		} else if (strncmp(key, "ipv6", nkey) == 0) {
			ipv6 = lua_toboolean(L, -1);
		} else if (strncmp(key, "reuseaddr", nkey) == 0) {
			reuseaddr = lua_toboolean(L, -1);
		} else if (strncmp(key, "reuseport", nkey) == 0) {
			reuseport = lua_toboolean(L, -1);
		}
		lua_pop(L, 1);
	}

	struct sockaddr_in6 addr6;
	struct sockaddr_in  addr4;
	struct sockaddr* addr = NULL;
	bzero(&addr6, sizeof(addr6));
	bzero(&addr4, sizeof(addr4));

	int fd = sockets::createTcpNonblockingOrDie(ipv6? AF_INET6 : AF_INET);
	assert(fd >= 0);

	sockets::setReuseAddr(fd, reuseaddr);
	sockets::setReusePort(fd, reuseport);

	if (ipv6) {
		addr = sockets::fromIpPort(ip, port, &addr6);
	} else {
		addr = sockets::fromIpPort(ip, port, &addr4);
	}
	int ok = sockets::bind(fd, addr);
	assert(ok >= 0);
	ok = sockets::listen(fd);
	assert(ok >= 0);

	lua_pushinteger(L, fd);

	return 1;
}
static int lua_tcp_connect(lua_State* L)
{
	assert(lua_gettop(L) == 1);
	assert(lua_type(L, 1) == LUA_TTABLE);

	char     ip[64] = { "0.0.0.0" };
	uint16_t port   = 0;
	bool     ipv6   = false;
	
	lua_pushnil(L);
	while(lua_next(L, -2)) {
		assert(lua_type(L, -2) == LUA_TSTRING);
		size_t nkey = 0;
		const char* key = luaL_checklstring(L, -2, &nkey);
		if (strncmp(key, "ip", nkey) == 0) {
			size_t nvalue = 0;
			const char* value = luaL_checklstring(L, -1, &nvalue);
			strncpy(ip, value, nvalue);
		} else if (strncmp(key, "port", nkey) == 0) {
			port = (uint16_t)luaL_checkinteger(L, -1);
		} else if (strncmp(key, "ipv6", nkey) == 0) {
			ipv6 = lua_toboolean(L, -1);
		}
		lua_pop(L, 1);
	}

	struct sockaddr_in6 addr6;
	struct sockaddr_in  addr4;
	struct sockaddr* addr = NULL;
	bzero(&addr6, sizeof(addr6));
	bzero(&addr4, sizeof(addr4));

	int fd = sockets::createTcpNonblockingOrDie(ipv6? AF_INET6 : AF_INET);
	assert(fd >= 0);

	if (ipv6) {
		addr = sockets::fromIpPort(ip, port, &addr6);
	} else {
		addr = sockets::fromIpPort(ip, port, &addr4);
	}


	int r  = 0; // 0:init, 1:connecting, 2:retry, 3:fail
	int ok = sockets::connect(fd, addr);
	int savedErrno = (ok == 0) ? 0 : errno;
	
	switch (savedErrno) {
	case 0:
	case EINPROGRESS:
	case EINTR:
	case EISCONN:
		r = 1;
		break;
	case EAGAIN:
	case EADDRINUSE:
	case EADDRNOTAVAIL:
	case ECONNREFUSED:
	case ENETUNREACH:
		r = 2;
		break;
	case EACCES:
	case EPERM:
	case EAFNOSUPPORT:
	case EALREADY:
	case EBADF:
	case EFAULT:
	case ENOTSOCK:
	default:
		r = 3;
		break;
	}

	assert(r>0 && r<4);
	lua_pushinteger(L, r);
	lua_pushinteger(L, fd);

	return 2;
}
static int lua_tcp_close(lua_State* L)
{
	assert(lua_gettop(L) == 1);
	int fd = (int)luaL_checkinteger(L, 1);
	assert(fd >= 0);
	sockets::close(fd);
	return 0;
}
static int lua_tcp_setopts(lua_State* L)
{
	assert(lua_gettop(L) == 2);
	int fd = (int)luaL_checkinteger(L, 1);
	assert(fd >= 0);
	lua_pushnil(L);
	while(lua_next(L, -2)) {
		assert(lua_type(L, -2) == LUA_TSTRING);
		size_t nkey = 0;
		const char* key = luaL_checklstring(L, -2, &nkey);
		if (strncmp(key, "keepalive", nkey) == 0) {
			bool keepalive = lua_toboolean(L, -1);
			sockets::setKeepAlive(fd, keepalive);
		} else if (strncmp(key, "nodelay", nkey) == 0) {
			bool nodelay = lua_toboolean(L, -1);
			sockets::setTcpNoDelay(fd, nodelay);
		}
		lua_pop(L, 1);
	}
	return 0;
}
static int lua_tcp_write(lua_State* L)
{
	assert(lua_gettop(L) == 2);
	int fd = (int)luaL_checkinteger(L, 1);
	size_t count = 0;
	const char* buf = luaL_checklstring(L, 2, &count);
	ssize_t n = sockets::write(fd, buf, count);
	lua_pushinteger(L, n);
	return 1;
}
static int lua_tcp_read(lua_State* L)
{
	assert(lua_gettop(L) == 1);
	int fd = (int)luaL_checkinteger(L, 1);
	char buf[65536];
	ssize_t n = sockets::read(fd, buf, sizeof(buf));
	if (n < 0) {
		lua_pushnil(L);
	} else {
		lua_pushlstring(L, buf, n);
	}
	return 1;
}
static int lua_tcp_accept(lua_State* L)
{
	assert(lua_gettop(L) == 1);
	int listenfd = (int)luaL_checkinteger(L, 1);
	assert(listenfd >= 0);

	struct sockaddr_in6 addr6;
	int connfd = sockets::accept(listenfd, &addr6);
	
	if (connfd < 0) {
		lua_pushnil(L);
		if (errno == EMFILE) {
			lua_pushstring(L, "Too many open files");
		} else {
			lua_pushstring(L, "other error");
		}
	} else {
		char     ip[64] = { '\0' };
		uint16_t port   = 0;
		bool     ipv6   = false;

		struct sockaddr* addr = (struct sockaddr*)&addr6;
		sockets::toIp(ip, 64, addr);
		port = sockets::toPort(addr);
		ipv6 = addr->sa_family == AF_INET6;

		lua_pushinteger(L, connfd);
		lua_newtable(L);

		lua_pushstring(L, "ip");
		lua_pushstring(L, ip);
		lua_settable(L, -3);

		lua_pushstring(L, "port");
		lua_pushinteger(L, port);
		lua_settable(L, -3);

		lua_pushstring(L, "ipv6");
		lua_pushboolean(L, ipv6);
		lua_settable(L, -3);
	}
	return 2;
}

static int lua_tcp_selfconnect(lua_State* L)
{
	assert(lua_gettop(L) == 1);
	int fd = (int)luaL_checkinteger(L, 1);
	if (sockets::isSelfConnect(fd)) {
		lua_pushboolean(L, 1);
	} else {
		lua_pushboolean(L, 0);
	}
	return 1;
}


extern "C" int luaopen_driver_tcp_api(lua_State* L)
{
	luaL_checkversion(L);

	luaL_Reg funcs[] = {
		{ "listen",      lua_tcp_listen      },
		{ "connect",     lua_tcp_connect     },
		{ "close",       lua_tcp_close       },
		{ "setopts",     lua_tcp_setopts     },
		{ "write",       lua_tcp_write       },
		{ "read",        lua_tcp_read        },
		{ "accept",      lua_tcp_accept      },
		{ "selfconnect", lua_tcp_selfconnect },
		{  NULL,         NULL                }
	};

	luaL_newlibtable(L, funcs);
	luaL_setfuncs(L, funcs, 0);

	return 1;
}