local system = require "system"
local socket = require "socket"
local logger = require "logger"

local path   = "../bin/sum2"
local se = socket.exe.open({path=path, argv={"a", "b", "c"}, fd=6, active=true})
if not se then
	logger.info("open exe fail")
else
	logger.info("open exe ok", system.rlen(), system.qlen(), system.mlen())
end

local i = 1
while true do
	system.receive {
		[{"json", "cast", nil, nil, "exe", path}] = function(type, pattern, source, ref, exe, path, msg)
			logger.info("#------#", msg:sub(1, -2), "#------#")
			if #msg == 0 then
				se:close()
			end
		end,
		[{"after", 5000}] = function(type, session, ms)
			i = i + 1
			print("sleep 5s", i)
		end,
	}
end