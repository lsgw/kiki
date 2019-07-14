local system = require "system"
local http   = { }



local _HTTP_M = { _VERSION = 'HTTP 5.34' }
function _HTTP_M.recv(self)
    for i, worker in ipairs(self.workers) do
        if not self.request[worker] then
            local ref = system.ref()
            local succ, err = system.send(worker, "json", "call", system.self(), ref, "recv")
            assert(succ, tostring(err))
            self.request[worker] = ref
        end
    end
    return system.receive {
        [{"json", "resp", self.master}] = function(type, resp, master, ref, worker, id, req)
            assert(self.request[worker] == ref)
            self.request[worker] = nil
            return {worker=worker, id=id}, req
        end
    }
end
function _HTTP_M.send(self, cid, response)
    assert(cid.id and cid.worker)
    local check = false
    for i, worker in ipairs(self.workers) do
        if worker == cid.worker then
            check = true
        end
    end
    assert(check)
    local succ, err = system.send(cid.worker, "json", "cast", system.self(), "ref", "send", cid.id, response)
    assert(succ, tostring(err))
end
function _HTTP_M.close(self)
    system.cast(self.master, "json", "close")
    for i, worker in ipairs(self.workers) do
        system.cast(worker, "json", "close")
    end
    setmetatable(self, nil)
end

function http.server(opts)
	opts = opts or { }
    local master = system.call(system.env.launcher, "json", "launch", "port", ctx.handle(), "http", {type="httpl"})
    assert(master > 0)
    local workers = { }
    for i=1, system.env.thread.event, 1 do
    	local worker = system.call(system.env.launcher, "json", "launch", "port", ctx.handle(), "http", {type="httpd"})
    	assert(worker > 0)
    	table.insert(workers, worker)
    end
    assert(system.call(master, "json", "start", {ip=opts.ip, port=opts.port, ipv6=opts.ipv6 or false}, workers))
	for i, worker in ipairs(workers) do
		assert(system.call(worker, "json", "start", master))
	end
    local self = setmetatable({}, { __index = _HTTP_M })
    self.master = master
    self.workers = workers
    self.request = { }
    return self
end
return http
