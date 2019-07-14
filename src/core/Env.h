#ifndef ENV_H
#define ENV_H

#include <vector>
#include <string>

struct Env {
	struct Thread {
		int event;
		int worker;
	};
	struct Module {
		std::vector<std::string> path;
	};
	struct Lua {
		std::string path;
		std::string cpath;
		std::string loader;
	};
	struct Entry {
		std::string main;
		std::string args;
	};
	Env();
	~Env();
	bool load(const std::string& configfile);

	Env::Thread thread;
	Env::Module module;
	Env::Lua    lua;
	Env::Entry  entry;
	bool        daemon;
	bool        profile;

	uint32_t    launcher;
	uint32_t    logger;
	uint32_t    signal;
};

#endif