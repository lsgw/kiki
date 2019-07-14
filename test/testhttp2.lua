local system = require "system"
local logger = require "logger"
local http   = require "http"
local ip, port, ipv6 = ...

logger.info("testhttp2", ip, port, ipv6, "|", system.rlen(), system.qlen(), system.mlen())

local server = http.server({ip=ip, port=port, ipv6=ipv6})

while true do
	local cid, req = server:recv()
	logger.info(cid, req)
	server:send(cid, {status=200, header={tttt="ssdfsdfs"}, body="hello"})
end