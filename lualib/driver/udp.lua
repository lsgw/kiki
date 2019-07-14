local system = require "system"
local logger = require "logger"
local api    = require "driver.udp.api"
local udp    = ...
local cmd    = { }

do
	system.protocol.register({
        name   = "event",
        id     = ctx.protocol("event"),
        unpack = ctx.decode_ioevent,
    })
end

local _M = { _VERSION = 'UDP 5.34' }
function _M:update()
	ctx.register(self.fd, {write=self.write, read=self.read})
end
function _M:sendto(msg, addr)
	self.wcount = self.wcount + 1
	self.wbtyes = self.wbtyes + #msg
	return api.sendto(self.fd, msg, addr)
end
function _M:recvfrom()
	local msg, addr = api.recvfrom(self.fd)
	if msg then
		self.rcount = self.rcount + 1
		self.rbytes = self.rbytes + #msg
		table.insert(udp.cache, {msg, addr})
	end
end
function _M:cacheexist()
	return #(self.cache) > 0
end
function _M:cacheremove()
	return table.unpack(table.remove(self.cache, 1))
end
function _M:close()
	api.close(self.fd)
end
function _M:bind()
	ctx.bind(self.fd)
end
function _M:unbind()
	ctx.unbind(self.fd)
end


local function init(self)
	self.fd = api.open({ip=self.ip, port=self.port, ipv6=self.ipv6, reuseaddr=self.reuseaddr, reuseport=self.reuseport})
end
function cmd.start()
	udp.starttime = os.date("%Y-%m-%d %H:%M:%S")
	udp.rbytes    = 0
	udp.wbtyes    = 0
	udp.rcount    = 0
	udp.wcount    = 0
	udp.handle    = ctx.handle()
	udp.timeout   = udp.timeout and udp.timeout * 1000 or -1
	udp.active    = udp.active  or false
	udp.owner     = ctx.owner()
	udp.cache     = { }
	udp.write     = false
	udp.read      = true
	
	init(udp)
	setmetatable(udp, { __index = _M })
	udp:bind()
	udp:update()
	return 1, udp.fd
end
function cmd.close()
	udp.read  = false
	udp.write = false
	udp:unbind()
	udp:update()
	udp:close()
	system.exit()
end
function cmd.setopts(opts)
	if opts.active ~= nil then
		udp.active = opts.active
		if udp.active then
			for i, m in ipairs(udp.cache) do
				system.cast(udp.owner, "lua", "udp", m)
			end
			udp.cache = { }
			if not udp.read then
				udp.read = true
				udp:update()
			end
		end
	end
	if opts.owner then
		local destport = system.self()
		local oldowner = ctx.owner()
		local newowner = opts.owner
		local curowner = system.call(ctx.launcher(), "json", "chmod", destport, oldowner, newowner)
		assert(newowner==curowner)
		ctx.setowner(newowner)
		udp.owner = newowner
	end
	if opts.timeout then
		udp.timeout = opts.timeout and opts.timeout * 1000 or -1
	end
end
function cmd.info()
	local info = { addr = { } }
	info.addr.ip   = udp.ip
	info.addr.port = udp.port
	info.addr.ipv6 = udp.ipv6
	info.fd        = udp.fd
	info.reuseaddr = udp.reuseaddr
	info.reuseport = udp.reuseport
	info.active    = udp.active
	info.timeout   = udp.timeout
	info.handle    = udp.handle
	info.owner     = udp.owner
	info.starttime = udp.starttime
	info.rbytes    = udp.rbytes
	info.wbtyes    = udp.wbtyes
	info.rcount    = udp.rcount
	info.wcount    = udp.wcount
	return info
end
function cmd.sendto(msg, addr)
	return udp:sendto(msg, addr)
end
function cmd.recvfrom()
	if udp.active then
		return nil
	end
	if udp:cacheexist() then
		return udp:cacheremove()
	end
	if not udp.read then
		udp.read = true
		udp:update()
	end

	while true do
		local ok, msg, addr
		if udp.timeout < 0 then
			ok, msg, addr = system.receive {
				[{"event", nil, "read", udp.fd}] = function(type, eventtime, eventtype, eventfd)
					udp:recvfrom()
					if udp:cacheexist(nbyte) then
						return true, udp:cacheremove(nbyte)
					else
						return false, nil, nil
					end
				end,
			}
		else
			ok, msg, addr = system.receive {
				[{"event", nil, "read", udp.fd}] = function(type, eventtime, eventtype, eventfd)
					udp:recvfrom()
					if udp:cacheexist(nbyte) then
						return true, udp:cacheremove(nbyte)
					else
						return false, nil, nil
					end
				end,
				[{"after", udp.timeout}] = function(...)
					return true, nil, nil
				end,
			}
		end
		if ok then
			return msg, addr
		end
	end
	
end

while true do
	system.receive {
		[{"event", nil, "read", udp.fd}] = function(type, eventtime, eventtype, eventfd)
			udp:recvfrom()
			if udp:cacheexist() then
				if udp.active then
					system.cast(udp.owner, "lua", "udp", "socket", udp:cacheremove())
				else
					if udp.read then
						udp.read = false
						udp:update()
					end
				end
			end
		end,
		[{"lua"}] = function(type, pattern, source, ref, fun, ...)
			if cmd[fun] then
				local rets = {cmd[fun](...)}
				if pattern == "call" then
					system.send(source, type, "resp", system.self(), ref, table.unpack(rets))
				end
			else
				logger.error("udp driver", type, pattern, source, ref, ...)
			end
		end,
	}
end