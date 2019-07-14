local system = require "system"
local socket = require "socket"
local ip, port, ipv6 = ...

print("testudp1", ctx.handle(), "|", system.rlen(), system.qlen(), system.mlen())
local su = socket.udp.open({ip=ip, port=port, ipv6=ipv6, reuseaddr=true, reuseport=true, active=true})

local i = 1
while true do
	system.receive {
		[{"lua", "cast", nil, nil, "udp", "socket"}] = function(type, pattern, source, ref, udp, socket, msg, addr)
			print("testudp1", type, pattern, source, ref, udp, msg:sub(1, -2), addr.ip, addr.port, addr.ipv6)
			su:sendto(msg, addr)
			if msg == "exit\n" then
				su:close()
				system.exit()
			end
		end,
		[{"after", 5000}] = function(type, session, ms)
			i = i + 1
			print("sleep 5s", i)
		end,
	}
end