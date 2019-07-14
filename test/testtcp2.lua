local system = require "system"
local socket = require "socket"
local logger = require "logger"
local ip, port, ipv6 = ...

logger.info("testtcp2", ctx.handle(), "|", system.rlen(), system.qlen(), system.mlen())
local sl = socket.tcp.listen({ip=ip, port=port, ipv6=ipv6, reuseaddr=true, reuseport=true, active=false})

local i = 1
while true do
	local sc = sl:accept()
	logger.info("accept ok", sc)
	local msg = sc:read()
	logger.info("connection recv", msg:sub(1, -2))
	sc:write(msg)
	sc:close()
end