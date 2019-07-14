local system = require "system"
local socket = { udp = { }, tcp = { }, exe = { }, dns = { } }

local function assertaddr(opts)
	assert( type(opts)      == "table"   )
	assert( type(opts.ip)   == "string"  )
	assert( type(opts.port) == "number"  )
	assert( type(opts.ipv6) == "boolean" )
end

local _UDP_M = { _VERSION = 'UDP 5.34' }
-- opts = {active=true, timeout=-1, owner=system.self()}
function _UDP_M.setopts(self, opts)
    system.cast(self.id, "lua", "setopts", opts)
end

function _UDP_M.sendto(self, msg, addr)
    system.cast(self.id, "lua", "sendto", msg, addr)
end
function _UDP_M.recvfrom(self)
    return system.call(self.id, "lua", "recvfrom")
end
function _UDP_M.close(self)
    system.cast(self.id, "lua", "close")
    setmetatable(self, nil)
end
function _UDP_M.info(self)
     return system.call(self.id, "lua", "info")
end

-- opts = {ip="0.0.0.0", port=8080, ipv6=false, reuseaddr=true, reuseport=true, active=true, timeout=-1}
function socket.udp.open(opts)
	assertaddr(opts)
	local id = system.newport("udp", opts)
	assert(id > 0)
	local ok, fd = system.call(id, "lua", "start")
	assert(ok > 0)
    local self = setmetatable({}, { __index = _UDP_M })
    self.id   = id
    self.fd   = fd
    self.ip   = opts.ip
    self.port = opts.port
    self.ipv6 = opts.ipv6
    return self
end


----------------------------------------------------------------
local _TCP_M = { _VERSION = 'TCP 5.34' }

function _TCP_M.write(self, msg)
    assert(type(msg) == "string")
    assert(#msg > 0)
    system.cast(self.id, "lua", "write", msg)
end
function _TCP_M.read(self, nbyte)
    return system.call(self.id, "lua", "read", nbyte)
end
function _TCP_M.info(self)
     return system.call(self.id, "lua", "info")
end
function _TCP_M.close(self)
     system.cast(self.id, "lua", "close")
end
function _TCP_M.accept(self)
    assert(self.type == "listener")
    local fd, addr = system.call(self.id, "lua", "read")
    assert( type(fd)   == "number" )
    assert( type(addr) == "table"  )

    local opts = { type="connection", fd=fd, ip=addr.ip, port=addr.port, ipv6=addr.ipv6 }
    local id = system.newport("tcp", opts)
    assert(id > 0)
    local ok, fd = system.call(id, "lua", "start")
    assert(ok > 0)
    
    local conn = setmetatable({}, { __index = _TCP_M })
    conn.type  = "connection"
    conn.id    = id
    conn.fd    = fd
    conn.ip    = addr.ip
    conn.port  = addr.port
    conn.ipv6  = addr.ipv6

    return conn
end
-- opts = {keepalive=true, nodelay=true, active=true, timeout=-1, owner=system.self()}
function _TCP_M.setopts(self, opts)
    system.cast(self.id, "lua", "setopts", opts)
end
-- opts = {ip="0.0.0.0", port=8080, ipv6=false, reuseaddr=true, reuseport=true, keepalive=true, nodelay=true, active=true, timeout=-1}
function socket.tcp.listen(opts)
    assertaddr(opts)
    opts.type = "listener"
    local id = system.newport("tcp", opts)
    assert(id > 0)
    local ok, fd = system.call(id, "lua", "start")
    assert(ok > 0)
    local self = setmetatable({}, { __index = _TCP_M })
    self.type = "listener"
    self.id   = id
    self.fd   = fd
    self.ip   = opts.ip
    self.port = opts.port
    self.ipv6 = opts.ipv6
    return self
end
-- opts = {ip="0.0.0.0", port=8080, ipv6=false, keepalive=true, nodelay=true, active=true, timeout=-1}
function socket.tcp.connect(opts)
    assertaddr(opts)
    opts.type = "client"
    local id = system.newport("tcp", opts)
    assert(id > 0)
    local ok, fd = system.call(id, "lua", "start")
    if ok == 0 then
        system.cast(id, "lua", "close")
        return nil
    else
        local self = setmetatable({}, { __index = _TCP_M })
        self.type = "client"
        self.id   = id
        self.fd   = fd
        self.ip   = opts.ip
        self.port = opts.port
        self.ipv6 = opts.ipv6
        return self
    end
end


local _EXE_M = { _VERSION = 'EXE 5.34' }
function _EXE_M.write(self, msg)
    assert(type(msg) == "string")
    assert(#msg > 0)
    system.cast(self.id, "json", "send", msg)
end
function _EXE_M.read(self, nbyte)
    return system.call(self.id, "json", "recv", nbyte)
end
function _EXE_M.info(self)
     return system.call(self.id, "json", "info")
end
function _EXE_M.close(self)
    system.cast(self.id, "json", "close")
    setmetatable(self, nil)
end
-- opts = {path="../hell", argv={"a", "b"}, exefd=5, active=true, timeout=-1}
function socket.exe.open(opts)
    assert(type(opts) == "table")
    opts = opts or { }
    local id = system.call(system.env.launcher, "json", "launch", "port", ctx.handle(), "exe", {path=opts.path, argv=opts.argv, fd=opts.fd, active=opts.active, timeout=opts.timeout})
    assert(id > 0)
    local ok, pid, fd = system.call(id, "json", "start")
    if ok == 0 then
        system.cast(id, "json", "exit")
        return nil
    else
        local self = setmetatable({}, { __index = _EXE_M })
        self.id   = id
        self.pid  = pid
        self.fd   = fd
        self.path = opts.path
        self.argv = opts.argv
        return self
    end
end



local _DNS_M = { _VERSION = 'DNS 5.34' }
function _DNS_M.resolve(self, hostname)
    return system.call(self.id, "json", "resolve", hostname)
end
function _DNS_M.close(self)
    system.cast(self.id, "json", "close")
    setmetatable(self, nil)
end
function socket.dns.open(opts)
    opts = opts or { }
    local id = system.call(system.env.launcher, "json", "launch", "port", ctx.handle(), "dns", {ip=opts.ip, port=opts.port, ipv6=opts.ipv6})
    assert(id > 0)
    local ok, fd = system.call(id, "json", "start")
    assert(ok)
    local self = setmetatable({}, { __index = _DNS_M })
    self.id = id
    self.fd = fd
    return self
end
return socket