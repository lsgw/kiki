local system = require "system"
local logger = require "logger"
local socket = require "socket"

local se = socket.exe.open({path="../bin/sum3", argv={"a", "b"}, active=false})
if not se then
	logger.info("open exe fail")
else
	logger.info("open exe ok", system.rlen(), system.qlen(), system.mlen())
end

while true do
	local msg = se:read()
	logger.info("se:", msg)
	if #msg == 0 then
		se:close()
		break
	end
end
