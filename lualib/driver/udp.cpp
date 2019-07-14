#include "lua/lua.hpp"
#include "sockets.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>


static int lua_udp_open(lua_State* L)
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

	int fd = sockets::createUdpNonblockingOrDie(ipv6? AF_INET6 : AF_INET);
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
	lua_pushinteger(L, fd);

	return 1;
}

static int lua_udp_close(lua_State* L)
{
	assert(lua_gettop(L) == 1);
	int fd = (int)luaL_checkinteger(L, 1);
	assert(fd >= 0);
	sockets::close(fd);
	return 0;
}
static int lua_udp_sendto(lua_State* L)
{
	assert(lua_gettop(L) == 3);

	int fd = (int)luaL_checkinteger(L, 1);
	assert(fd >= 0);

	size_t nbyte = 0;
	const char* udata = luaL_checklstring(L, 2, &nbyte);

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

	socklen_t len = 0;
	struct sockaddr_in6 addr6;
	struct sockaddr_in  addr4;
	struct sockaddr* addr = NULL;
	bzero(&addr6, sizeof(addr6));
	bzero(&addr4, sizeof(addr4));

	if (ipv6) {
		len  = sizeof(addr6);
		addr = sockets::fromIpPort(ip, port, &addr6);
	} else {
		len  = sizeof(addr4);
		addr = sockets::fromIpPort(ip, port, &addr4);
	}

	int n = sendto(fd, udata, nbyte, 0, addr, len);
	lua_pushinteger(L, n);
	
	return 1;
}
static int lua_udp_recvfrom(lua_State* L)
{
	assert(lua_gettop(L) == 1);
	int fd = (int)luaL_checkinteger(L, 1);
	char buf[65536];
	struct sockaddr_in6 addr6;
	socklen_t        len  = sizeof(addr6);
	struct sockaddr* addr = (struct sockaddr*)&addr6;

	char ip[64] = { '\0' };
	uint16_t port = 0;
	bool ipv6 = false;

	ssize_t n = recvfrom(fd, buf, sizeof(buf), 0, addr, &len);
	if (n < 0) {
		lua_pushnil(L);
		lua_pushnil(L);
	} else {
		sockets::toIp(ip, 64, addr);
		port = sockets::toPort(addr);
		ipv6 = addr->sa_family == AF_INET6;

		lua_pushlstring(L, buf, n);
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


extern "C" int luaopen_driver_udp_api(lua_State* L)
{
	luaL_checkversion(L);

	luaL_Reg funcs[] = {
		{ "open",     lua_udp_open     },
		{ "close",    lua_udp_close    },
		{ "sendto",   lua_udp_sendto   },
		{ "recvfrom", lua_udp_recvfrom },
		{  NULL,      NULL             }
	};

	luaL_newlibtable(L, funcs);
	luaL_setfuncs(L, funcs, 0);

	return 1;
}