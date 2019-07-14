#include "Reactor.h"
#include "Env.h"
#include <string>

int main(int argc, char* argv[])
{
	if (argc < 2) {
		fprintf(stderr, "Need a config file. usage: targetssnet configfile\n");
		return 1;
	}

	std::string configfile(argv[1]);
	if (configfile.empty()) {
		fprintf(stderr, "Need a config file. usage: targetssnet configfile\n");
		return 2;
	}

	Env env;
	if (!env.load(configfile)) {
		fprintf(stderr, "load configfile error\n");
		return 3;
	}

	Reactor reactor;
	reactor.start(env);
	reactor.loop();

	return 0;
}