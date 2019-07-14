#include "Module.h"
#include <stdio.h>
#include <dlfcn.h>

Module::Module(const std::string& name, void* dl, module_create create, module_callback callback, module_release release) :
	name_(name),
	dl_(dl),
	create_(create),
	callback_(callback),
	release_(release)
{
	fprintf(stderr, "create  - %s\n", name_.c_str());
}

Module::~Module()
{
	fprintf(stderr, "destroy - %s\n", name_.c_str());
}

void ModuleList::setModulePath(const std::vector<std::string>& pathname)
{
	paths_ = pathname;
	for (auto path : paths_) {
		fprintf(stderr, "module path - %s\n", path.c_str());
	}
}

bool ModuleList::reload(const std::string& name)
{
	lock_.wrlock();
	auto it = modules_.find(name);
	if (it != modules_.end()) {
		modules_.erase(name);
	}
	lock_.unlock();

	for (auto pathname : paths_) {
		pathname.replace(pathname.find("?"), 1, name);
		void* dl = dlopen(pathname.c_str(), RTLD_NOW | RTLD_GLOBAL);
		if (dl) {
			module_create   create   = (module_create)  dlsym(dl, "actor_create");
			module_callback callback = (module_callback)dlsym(dl, "actor_callback");
			module_release  release  = (module_release) dlsym(dl, "actor_release");
			if (create && callback && release) {
				dlclose(dl);
				return true;
			}
			dlclose(dl);
		}
	}
	return false;
}

std::shared_ptr<Module> ModuleList::query(const std::string& name)
{
	std::shared_ptr<Module> m;
	
	lock_.rdlock();
	auto it = modules_.find(name);
	if (it != modules_.end() && !it->second.expired()) {
		m = it->second.lock();
		
	} else {
		
	}
	lock_.unlock();

	if (m) {
		return m;
	}
	
	for (auto pathname : paths_) {
		pathname.replace(pathname.find("?"), 1, name);
		void* dl = dlopen(pathname.c_str(), RTLD_NOW | RTLD_GLOBAL);
		if (dl) {
			
			module_create   create   = (module_create)  dlsym(dl, "actor_create");
			module_callback callback = (module_callback)dlsym(dl, "actor_callback");
			module_release  release  = (module_release) dlsym(dl, "actor_release");

			if (create && callback && release) {
				m.reset(new Module(name, dl, create, callback, release), [this](Module* p) {
					
					lock_.wrlock();
					modules_.erase(p->name());
					lock_.unlock();

					dlclose(p->dl());
					delete p;
				});

				lock_.wrlock();
				modules_[name] = std::weak_ptr<Module>(m);
				lock_.unlock();
				
				return m;
			}
			dlclose(dl);
		}
	}
	
	return m;
}