local system = require "system"
local signal = require "signal"
local logger = require "logger"

signal.subscribe("SIGUSR1")
logger.info("testsig1", system.rlen(), system.qlen(), system.mlen())


local i = 1
local n = 1
while true do
	system.receive {
		[{"json", "cast", nil, nil, "signal"}] = function(type, pattern, source, ref, fun, signame)
			logger.info(type, pattern, source, ref, fun, signame)
			n = n + 1
			if n > 5 then
				signal.unsubscribe("SIGUSR1")
			end
		end,
		[{"after", 5000}] = function(type, session, ms)
			i = i + 1
			logger.info("sleep 5s", i, system.rlen(), system.qlen(), system.mlen())
		end,
	}
end