local system = require "system"
local json   = require "rapidjson"

local logger = { handle = system.env.logger, level = {
	trace = 0,   --指出比DEBUG粒度更细的一些信息事件 (开发过程使用)
	debug = 1,   --指出细粒度信息事件对调试应用程序是非常有帮助（开发过程使用)
	info  = 2,   --表明消息在粗粒度级别上突出强调应用程序的运行过程
	warn  = 3,   --系统能正常运行，但可能会出现潜在错误的情形
	error = 4,   --指出虽然发生错误事件，但仍然不影响系统的继续运行
	fatal = 5,   --指出每个严重的错误事件将会导致应用程序的退出
}}

do
    system.protocol.register({
        name = "log",
        id   = ctx.protocol("log"),
        pack = ctx.encode_logger,
    })
end

function logger.write(level, ...)
    local msg = ""
    for i, value in pairs({...}) do
        if type(value) == "table" then
            msg = msg .. json.encode(value) .. ","
        else
            msg = msg .. tostring(value) .. ","
        end
    end
    local info = debug.getinfo(3, "Sl")
    msg = "[" .. os.date("%Y-%m-%d %H:%M:%S") .. " " .. string.match(info["source"], ".+/([^/]*%.%w+)$") .. " " .. tostring(info["currentline"]) .. "] ".. msg:sub(1, -2)
    system.send(logger.handle, "log", level, msg)
end
function  logger.trace(...)
    logger.write(logger.level.trace, ...)
end
function  logger.debug(...)
    logger.write(logger.level.debug, ...)
end
function  logger.info(...)
    logger.write(logger.level.info, ...)
end
function  logger.warn(...)
    logger.write(logger.level.warn, ...)
end
function  logger.error(...)
    logger.write(logger.level.error, ...)
end
function  logger.fatal(...)
    logger.write(logger.level.fatal, ...)
    system.exit()
end

return logger