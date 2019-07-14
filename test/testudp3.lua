local system = require "system"
local socket = require "socket"
local logger = require "logger"
local ip, port, ipv6 = ...

logger.info("testudp3", ctx.handle(), "|", system.rlen(), system.qlen(), system.mlen())
local su = socket.udp.open({ip=ip, port=port, ipv6=ipv6, reuseaddr=true, reuseport=true, active=false})

while true do
	local msg, addr = su:recvfrom()
	logger.info("testudp3",  msg, addr)
	if msg == "exit\n" then
		su:close()
		break
	end
end