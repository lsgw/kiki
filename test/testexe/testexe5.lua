local system = require "system"
local logger = require "logger"
local socket = require "socket"

local se = socket.exe.open({path="../bin/sum5", argv={"a", "b", "c"}, fd=6, active=false})
if not se then
	logger.info("open exe fail")
else
	logger.info("open exe ok", system.rlen(), system.qlen(), system.mlen())
end


local i = 0
while true do
	local msg = se:read()
	logger.info(#msg, msg)
	if #msg == 0 then
		se:close()
		break
	end
end
