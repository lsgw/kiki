#include "Env.h"
#include "utils.h"
#include "nlohmann/json.hpp"
#include <fstream>
#include <vector>
#include <string>

Env::Env() :
	launcher(0),
	logger(0),
	signal(0)
{

}
Env::~Env()
{
	
}
bool Env::load(const std::string& configfile)
{
	nlohmann::json j;
	std::ifstream ifs(configfile);
	if (!ifs.is_open()) {
		fprintf(stderr, "open config error\n");
		return false;
	}
	ifs >> j;
	assert(j.is_object());
	assert(j["daemon"].is_boolean());
	daemon = j["daemon"].get<bool>();
	if (daemon) {
		if (utils::becomeDaemon() < 0) {
			fprintf(stderr, "become daemon error\n");
			return false;
		}
	}

	assert(j["profile"].is_boolean());
	profile = j["profile"].get<bool>();

	assert(j["thread"].is_object());
	assert(j["thread"]["event"].is_number());
	assert(j["thread"]["worker"].is_number());
	thread.event  = j["thread"]["event"].get<int>();
	thread.worker = j["thread"]["worker"].get<int>();

	assert(j["module"].is_object());
	assert(j["module"]["path"].is_array());
	std::vector<std::string> path;
	for (int i=0; i<j["module"]["path"].size(); i++) {
		assert(j["module"]["path"][i].is_string());
		module.path.push_back(j["module"]["path"][i].get<std::string>());
	}

	assert(j["lua"].is_object());
	assert(j["lua"]["path"].is_array());
	assert(j["lua"]["cpath"].is_array());
	assert(j["lua"]["loader"].is_string());
	for (int i=0; i<j["lua"]["path"].size(); i++) {
		assert(j["lua"]["path"][i].is_string());
		lua.path += j["lua"]["path"][i].get<std::string>();
		lua.path += ";";
	}
	for (int i=0; i<j["lua"]["cpath"].size(); i++) {
		assert(j["lua"]["cpath"][i].is_string());
		lua.cpath += j["lua"]["cpath"][i].get<std::string>();
		lua.cpath += ";";
	}
	lua.loader = j["lua"]["loader"].get<std::string>();

	assert(j["entry"].is_object());
	assert(j["entry"]["main"].is_string());
	assert(j["entry"]["args"].is_array());
	entry.main = j["entry"]["main"].get<std::string>();
	entry.args = j["entry"]["args"].dump();
	
	return true;
}