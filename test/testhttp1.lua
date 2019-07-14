local system = require "system"
local logger = require "logger"
local socket = require "socket"

logger.info("testdns1", system.rlen(), system.qlen(), system.mlen())

local server = socket.http.server({ip="127.0.0.1", port=8181})

while true do
	logger.info("hello", server)
	system.sleep(1)
end