package.path , LUA_PATH = LUA_PATH
package.cpath , LUA_CPATH = LUA_CPATH

local json = require "rapidjson"
local args = json.decode(...)

assert(#args>0, "need lua file to run")

SERVICE_NAME = args[1]

local main, pattern

local err = {}
for pat in string.gmatch(package.path, "([^;]+);*") do
	local filename = string.gsub(pat, "?", SERVICE_NAME)
	local f, msg = loadfile(filename)
	if not f then
		table.insert(err, msg)
	else
		pattern = pat
		main = f
		break
	end
end

if not main then
	error(table.concat(err, "\n"))
end


function dispatch(...)
	return main(...)
end

setmetatable(_G, {
    __newindex = function(_, name, value)
        error("disable create unexpected global variable : "..tostring(name).."="..tostring(value) )
    end
})


return dispatch(table.unpack(args, 2))



