local system = require "system"
local socket = require "socket"
local logger = require "logger"

logger.info("testdns1", system.rlen(), system.qlen(), system.mlen())


local dns = socket.dns.open()
local resolvelist = {
	"stackoverflow.com",
	"www.baidu.com",
	"github.com",
	"lua.com",
}

local i = 1
while true do
	for _ , hostname in ipairs(resolvelist) do
		logger.info(hostname, dns:resolve(hostname))
	end
	i = i + 1
	logger.info(string.format("sleep 1s count=%d", i))
	system.sleep(1)
end
dns:close()
