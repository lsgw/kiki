local system = require "system"
local logger = require "logger"
local api    = require "driver.tcp.api"
local tcp    = ...
local cmd    = { }

do
	system.protocol.register({
        name   = "event",
        id     = ctx.protocol("event"),
        unpack = ctx.decode_ioevent,
    })
end

local _M = { _VERSION = 'TCP 5.34' }
local _M_client     = { }
local _M_listener   = { }
local _M_connection = { }



function _M_client.connecting(fd)
	ctx.register(fd, {write=true, read=false})
	return system.receive {
		[{"event", nil, "write", fd}] = function(type, eventtime, eventtype, eventfd)
			ctx.register(fd, {write=false, read=false})
			return true
		end,
		[{"after", 5000}] = function(type, session, ms)
			ctx.register(fd, {write=false, read=false})
			return false
		end,
	}
end
function _M_client.selfconnect(fd)
	return api.selfconnect(fd)
end

-- opts = {ip=self.ip, port=self.port, ipv6=self.ipv6, keepalive=self.keepalive, nodelay=self.nodelay}
function _M_client.connector(opts)
	local n = 5 -- retry count 
	local i = 0
	while i < n do
		i = i + 1
		local r, fd = api.connect({ip=opts.ip, port=opts.port, ipv6=opts.ipv6})
		if r == 1 and _M_client.connecting(fd) and not _M_client.selfconnect(fd) then
			opts.status = 1
			opts.fd     = fd
			api.setopts(fd, {keepalive=opts.keepalive, nodelay=opts.nodelay})
			break
		elseif r == 2 then -- retry
			opts.status = 0
			opts.fd     = nil
			api.close(fd)
		else              -- r=1, r=3 fail
			opts.status = 0
			opts.fd     = nil
			api.close(fd)
			break
		end
		system.sleep(1)
	end
end




function _M_listener:send(msg)
	msg = msg or ""
	return #msg
end
function _M_listener:accept()
	local fd, addr = api.accept(self.fd)
	if fd then
		table.insert(self.cache, {fd, addr})
		self.rcount = self.rcount + 1
	end
end
function _M_listener:cacheexist(nbyte)
	return #(self.cache) > 0
end
function _M_listener:cacheremove(nbyte)
	return table.unpack(table.remove(self.cache, 1))
end


function _M_connection:send(msg)
	self.wcount = self.wcount + 1
	self.wbtyes = self.wbtyes + #msg
	return api.write(self.fd, msg)
end
function _M_connection:recv()
	local msg = api.read(self.fd)
	if msg then
		self.rcount = self.rcount + 1
		self.rbytes = self.rbytes + #msg
		self.cache  = self.cache .. msg
		if #msg == 0 then
			self.status = 2
			self.read = false
			ctx.register(self.fd, {write=self.write, read=self.read})
		end
	end
end
function _M_connection:cacheexist(nbyte)
	if self.status == 2 then
		return true
	end
	if nbyte < 0 then
		nbyte = 1
	end
	return #(self.cache) >= nbyte
end
function _M_connection:cacheremove(nbyte)
	if self.status == 2 then
		return ""
	end
	local rets = string.sub(self.cache, 1, nbyte)
	self.cache = string.sub(self.cache, #rets+1, -1)
	return rets
end

function _M:update()
	ctx.register(self.fd, {write=self.write, read=self.read})
end
function _M:setopts()
	api.setopts(self.fd, {keepalive=self.keepalive, nodelay=self.nodelay})
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
	if self.type == nil or (self.type~="listener" and self.type~="client" and self.type~="connection") then
		self.status     = 0
		self.cache      = nil
		_M.send         = nil
		_M.recv         = nil
		_M.cacheexist   = nil
		_M.cacheremove  = nil

	elseif self.type == "listener"   then
		self.status     = 1
		self.cache      = { }
		self.fd         = api.listen({ip=self.ip, port=self.port, ipv6=self.ipv6, reuseaddr=self.reuseaddr, reuseport=self.reuseport})
		_M.send         = _M_listener.send
		_M.recv         = _M_listener.accept
		_M.cacheexist   = _M_listener.cacheexist
		_M.cacheremove  = _M_listener.cacheremove

	elseif self.type == "client"  then
		self.status     = 0
		self.cache      = ""
		_M_client.connector(self)
		_M.send         = _M_connection.send
		_M.recv         = _M_connection.recv
		_M.cacheexist   = _M_connection.cacheexist
		_M.cacheremove  = _M_connection.cacheremove

	elseif self.type == "connection" then
		self.status     = 1
		self.cache      = ""
		_M.send         = _M_connection.send
		_M.recv         = _M_connection.recv
		_M.cacheexist   = _M_connection.cacheexist
		_M.cacheremove  = _M_connection.cacheremove

	end
end

function cmd.start()
	tcp.starttime = os.date("%Y-%m-%d %H:%M:%S")
	tcp.rbytes    = 0
	tcp.wbtyes    = 0
	tcp.rcount    = 0
	tcp.wcount    = 0
	tcp.handle    = ctx.handle()
	tcp.timeout   = tcp.timeout and tcp.timeout * 1000 or -1
	tcp.active    = tcp.active  or false
	tcp.owner     = ctx.owner()
	tcp.write     = false
	tcp.read      = true
	tcp.status    = 0

	setmetatable(tcp, { __index = _M })
	init(tcp)
	if tcp.status == 1 then
		tcp:bind()
		tcp:update()
	end
	return tcp.status, tcp.fd
end
function cmd.close()
	tcp.read  = false
	tcp.write = false
	if tcp.fd then
		tcp:unbind()
		tcp:update()
		tcp:close()
	end
	system.exit()
end

function cmd.setopts(opts)
	if opts.active ~= nil then
		tcp.active = opts.active
		if tcp.active then
			local nbyte = -1
			while tcp:cacheexist(nbyte) do
				system.cast(tcp.owner, "lua", "tcp", tcp.type, tcp:cacheremove(nbyte))
			end
			if not tcp.read then
				tcp.read = true
				tcp:update()
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
		tcp.owner = newowner
	end
	if opts.keepalive ~= nil then
		tcp.keepalive = opts.keepalive
	end
	if opts.nodelay ~= nil then
		tcp.nodelay = opts.nodelay
	end
	if opts.keepalive ~= nil or opts.nodelay ~= nil then
		tcp:setopts()
	end
	if opts.timeout then
		tcp.timeout = opts.timeout and opts.timeout * 1000 or -1
	end
end

function cmd.info()
	local info = { addr = { } }
	info.addr.ip   = tcp.ip
	info.addr.port = tcp.port
	info.addr.ipv6 = tcp.ipv6
	info.fd        = tcp.fd
	info.reuseaddr = tcp.reuseaddr
	info.reuseport = tcp.reuseport
	info.keepalive = tcp.keepalive
	info.nodelay   = tcp.nodelay
	info.active    = tcp.active
	info.timeout   = tcp.timeout
	info.handle    = tcp.handle
	info.owner     = tcp.owner
	info.starttime = tcp.starttime
	info.rbytes    = tcp.rbytes
	info.wbtyes    = tcp.wbtyes
	info.rcount    = tcp.rcount
	info.wcount    = tcp.wcount
	return info
end

function cmd.write(msg)
	local a = #msg
	local n = tcp:send(msg)
	if n == #msg then
		return a
	end
	if n < 0 then
		n = 0
	end
	msg = string.sub(msg, n+1, -1)
	tcp.write = true
	tcp:update()
	while true do
		local ok = system.receive {
			[{"event", nil, "write", tcp.fd}] = function(type, eventtime, eventtype, eventfd)
				local n = tcp:send(msg)
				if n == #msg then
					return true
				end
				if n < 0 then
					n = 0
				end
				msg = string.sub(msg, n+1, -1)
				return false
			end,
		}
		if ok then
			break
		end
	end
	tcp.write = false
	tcp:update()
	return a
end
function cmd.read(nbyte)
	if tcp.active then
		return nil, "active is true"
	end
	nbyte = nbyte or -1
	if nbyte == 0 then
		return nil, "read nbyte is 0"
	end
	if tcp:cacheexist(nbyte) then
		return tcp:cacheremove(nbyte)
	end
	if not tcp.read then
		tcp.read = true
		tcp:update()
	end
	while true do
		local ok, ret1, ret2
		if tcp.timeout < 0 then
			ok, ret1, ret2 = system.receive {
				[{"event", nil, "read", tcp.fd}] = function(type, eventtime, eventtype, eventfd)
					tcp:recv()
					if tcp:cacheexist(nbyte) then
						return true, tcp:cacheremove(nbyte)
					else
						return false, nil, nil
					end
				end,
			}
		else
			ok, ret1, ret2 = system.receive {
				[{"event", nil, "read", tcp.fd}] = function(type, eventtime, eventtype, eventfd)
					tcp:recv()
					if tcp:cacheexist(nbyte) then
						return true, tcp:cacheremove(nbyte)
					else
						return false, nil, nil
					end
				end,
				[{"after", tcp.timeout}] = function(...)
					return true, nil, "timeout"
				end,
			}
		end

		if ok then
			return ret1, ret2
		end
	end
end

while true do
	system.receive {
		[{"event", nil, "read", tcp.fd}] = function(type, eventtime, eventtype, eventfd)
			local nbyte = -1
			tcp:recv()
			if tcp:cacheexist(nbyte) then
				if tcp.active then
					system.cast(tcp.owner, "lua", "tcp", tcp.type, tcp:cacheremove(nbyte))
				else
					logger.info("tcp.cache")
					if tcp.read then
						tcp.read = false
						tcp:update()
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
				logger.error("tcp driver", type, pattern, source, ref, ...)
			end
		end,
	}
end
