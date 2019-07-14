local system = require "system"
local socket = require "socket"
local logger = require "logger"
local ip, port, ipv6 = ...

logger.info("testtcp1", ctx.handle(), "|", system.rlen(), system.qlen(), system.mlen())
local sc = socket.tcp.connect({ip=ip, port=port, ipv6=ipv6, keepalive=true, nodelay=true, active=false, timeout=5})
logger.info("sc=", sc)
if not sc then
	logger.info("connect false")
	system.exit()
end

local i = 1
while true do
	local msg, err = sc:read()
	if not msg or #msg == 0 then
		logger.info("connection recv close", err, sc)
		break
	elseif msg == "exit\n" then
		logger.info("connection recv message", msg:sub(1, -2))
		sc:close()
		break
	else
		logger.info("connection recv message", msg:sub(1, -2))
		sc:write(msg)
	end
end

logger.info("sleep 55555")
system.sleep(5)