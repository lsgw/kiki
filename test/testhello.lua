local system = require "system"

print("testhello", ctx.handle(), "|", system.rlen(), system.qlen(), system.mlen(), "|", ...)

local i = 1
while true do
	system.receive {
		[{"after", 5000}] = function(type, session, ms)
			i = i + 1
			print(string.format("i=%d, type=%d, session=%d, ms=%d", i, type, session, ms))
			if i > 4 then
				system.exit()
			end
		end,
	}
end