local system = require "system"
local logger = require "logger"
local socket = require "socket"

local se = socket.exe.open({path="../bin/sum4", argv={"a", "b", "c"}, active=false})
if not se then
	logger.info("open exe fail")
else
	logger.info("open exe ok", system.rlen(), system.qlen(), system.mlen())
end

se:write("in test exe4")
local msg = se:read()
logger.info("read msg", msg)
se:close()

