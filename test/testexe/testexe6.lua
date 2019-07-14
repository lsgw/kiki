local system = require "system"
local logger = require "logger"
local socket = require "socket"
local path   = "../bin/sum6"

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
			logger.info("#------#", msg, "#------#")
			if #msg == 0 then
				se:close()
			end
		end,
		[{"after", 1000}] = function(type, session, ms)
			i = i + 1
			logger.info("i", i)
			se:write(tostring(i) .. " " .. tostring(i+1))
			if i > 4 then
				se:close()
				system.exit()
			end
		end,
	}
end