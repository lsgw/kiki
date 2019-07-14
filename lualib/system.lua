local json  = require "rapidjson"
local seri  = require "serialize"
local system = { protocol = { }, env = ctx.env() }

function system.protocol.register(class)
 	local name = class.name
	local id = class.id
	assert(system.protocol[name] == nil and system.protocol[id] == nil)
	assert(type(name) == "string" and type(id) == "number" and id >=1 and id <=255)
	system.protocol[name] = class
	system.protocol[id] = class
end

do
	system.protocol.register({
		name     = "json",
		id       = ctx.protocol("json"),
		pack     = function(...) return json.encode({...}) end,
		unpack   = function(...) return table.unpack(json.decode(...)) end,
	})
	system.protocol.register({
		name     = "lua",
		id       = ctx.protocol("lua"),
		pack     = seri.encode,
		unpack   = seri.decode,
	})
	system.protocol.register({
        name     = "time",
        id       = ctx.protocol("time"),
        unpack   = function (...) return ctx.decode_timeout(...) end,
        dispatch = function(type, session, ms)
        end
    })
end


local function format(t)
	local m = { }
	for key, fun in pairs(t) do
		assert(type(key)=="table" and type(fun)=="function")
		if #key>0 and type(key[1])=="string" and key[1]=="after" then
			assert(type(key[2])=="number")
			local ms = key[2];
			local session = ctx.newsession();
			ctx.timeout(session, ms)
			local typeid = system.protocol["time"].id
			m[{typeid, session, ms}] = fun
		elseif #key>0 and type(key[1])=="string" then
			local typeid = system.protocol[key[1]].id
			m[{typeid, table.unpack(key, 2)}] = fun
		else
			m[key] = fun
		end
	end
	return m
end

local function equals(wait, recv)
	assert(type(wait) == "table")
    for i, _ in pairs(wait) do
        if wait[i] ~= recv[i] then
			return false
        end
    end
    return true
end
local function kpairs(t)
	local a = {}
	for n in pairs(t) do
		a[#a + 1] = n
	end
	table.sort(a, function(l, r) return #l > #r end)
	local i = 0
	return function()
		i = i + 1
		return a[i], t[a[i]]
	end
end
local function dispatch(type, ...)
	local p = system.protocol[type]
	if p and p.dispatch then
		p.dispatch(type, ...)
		return true
	else
		return false
	end
end

function system.receive(t)
	local m = format(t)
    local f = true
    while true do
        local message = coroutine.yield(f)
        local recv_type, recv_udata, recv_nbyte = ctx.unpack(message)
        local recv = {recv_type, system.protocol[recv_type].unpack(recv_udata, recv_nbyte)}
        for wait, fun in kpairs(m) do
            if equals(wait, recv) then
                f = true
				return fun(table.unpack(recv))
            end
        end
        if dispatch(table.unpack(recv)) then
            f = true
        else
        	f = false
        end
    end
end

function system.self()
    return ctx.handle()
end
function system.now() -- micros
	return ctx.now()
end
function system.owner()
	return ctx.owner()
end
function system.reload(module)
	return ctx.reload(module)
end
function system.rlen()
	return ctx.rlen()
end
function system.qlen()
	return ctx.qlen()
end
function system.mlen()
	return ctx.mlen()
end
function system.cpucost()
	return ctx.cpucost()
end
function system.recvcount()
	return ctx.recvcount()
end
function system.profile()
	return ctx.profile()
end
function system.ref()
	return tostring(ctx.handle()) .. "-" .. tostring(ctx.newsession()) .. "-" .. tostring(ctx.now())
end


function system.send(handle, type, ...)
	assert(system.protocol[type], "can't find "..type.." protocol")
	assert(handle~=ctx.handle(), "can't send self message")
	local p = system.protocol[type]
	return ctx.send(handle, p.id, p.pack(...))
end
function system.call(handle, type, ...)
	local ref = system.ref()
    local succ, err = system.send(handle, type, "call", system.self(), ref, ...)
	assert(succ, tostring(err))
	return system.receive {
		[{type, "resp", handle, ref}] = function(type, resp, source, ref, ...)
			return ...
		end
	}
end
function system.cast(handle, type, ...)
	local ref = "ref"
	local succ, err = system.send(handle, type, "cast", system.self(), ref, ...)
	--assert(succ, tostring(err))
end



assert(system.env.launcher == 1)
function system.register(handle, name)
	assert(type(handle)=="number" and type(name) == "string", "param need : uint32_t handle, string name")
    return system.call(1, "json", "register", handle, name)
end
function system.query(name)
	assert(type(name)=="string", "service name must be an string")
	return system.call(1, "json", "query", name)
end
function system.monitor(handle)
	assert(type(handle)=="number", "service handle must be an number")
	return system.call(1, "json", "monitor", true, ctx.handle(), handle)
end
function system.demonitor(handle)
	assert(type(handle)=="number", "service handle must be an number")
	return system.call(1, "json", "monitor", false, ctx.handle(), handle)
end
function system.link(handle)
	assert(type(handle)=="number", "service handle must be an number")
	return system.call(1, "json", "link", true, ctx.handle(), handle)
end
function system.unlink(handle)
	assert(type(handle)=="number", "service handle must be an number")
	return system.call(1, "json", "link", false, ctx.handle(), handle)
end
function system.kill(handle)
	assert(type(handle)=="number", "service handle must be an number")
	return system.cast(ctx.launcher(), "json", "kill", handle)
end

function system.newservice(...)
	return system.call(1, "json", "launch", "service", ctx.handle(), "Snlua", ...)
end
function system.newport(...)
	return system.call(1, "json", "launch", "port", ctx.handle(), "Snlua", ...)
end

function system.uniqueservice(name, ...)
	assert(type(name)=="string", "service name must be an string")
	local handle = system.call(1, "json", "query", name)
	if type(handle) == "number" then
		return handle
	else
		handle = system.call(1, "json", "launch", "service", "Snlua", name, ...)
	end
	assert(type(handle) == "number", "new service fail : " .. name)
	system.cast(1, "json", "register", handle, name)
	return handle
end

function system.sleep(second)
	assert(type(second) == "number")
	system.receive({ [{"after", second * 1000}] = function(...) end })
end

function system.exit()
	ctx.exit()
	while true do
		local message = coroutine.yield(true)
	end
end
function system.abort()
	ctx.abort()
	while true do
		local message = coroutine.yield(true)
	end
end

return system