local system = require "system"
local socket = require "socket"
local logger = require "logger"
local ip, port, ipv6 = ...

logger.info("testtcp2", ctx.handle(), "|", system.rlen(), system.qlen(), system.mlen())
local sl = socket.tcp.listen({ip=ip, port=port, ipv6=ipv6, reuseaddr=true, reuseport=true, active=true})

local i = 1
while true do
	system.receive {
		[{"lua", "cast", nil, nil, "tcp", "listener"}] = function(type, pattern, source, ref, tcp, listener, fd, addr)
			logger.info("testtcp1", type, pattern, source, ref, tcp, listener, fd, addr)
			local opts = { type="connection", fd=fd, ip=addr.ip, port=addr.port, ipv6=addr.ipv6, active=true }
			local id = system.newport("tcp", opts)
			assert(id > 0)
			local ok = system.call(id, "lua", "start")
			assert(ok > 0)
		end,
		[{"lua", "cast", nil, nil, "tcp", "connection"}] = function(type, pattern, source, ref, tcp, connection, msg)
			logger.info("testtcp1", type, pattern, source, ref, tcp, connection, msg)
			system.cast(source, "lua", "write", msg)
			if #msg == 0 then
				system.cast(source, "lua", "close")
			end
		end,
		[{"after", 5000}] = function(type, session, ms)
			i = i + 1
			logger.info("sleep 5s", i)
		end,
	}
end