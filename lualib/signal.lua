local system = require "system"
local signal = { handle = system.env.signal }

function signal.unsubscribe(signame, ...)
    assert(type(signame) == "string")
    system.cast(signal.handle, "json", "unsubscribe", system.self(), signame, ...)
end
function signal.subscribe(signame, ...)
    assert(type(signame) == "string")
    system.cast(signal.handle, "json", "subscribe", system.self(), signame, ...)
end

return signal
