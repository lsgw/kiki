#include "Monitor.h"
#define ATOM_INC(ptr) __sync_add_and_fetch(ptr, 1)
#define ATOM_DEC(ptr) __sync_add_and_fetch(ptr, -1)
#define ATOM_GET(ptr) __sync_val_compare_and_swap(ptr, 0, 0)
#define ATOM_SET(ptr,val) __sync_lock_test_and_set(ptr, val)

Monitor::Watcher::Watcher() :
	weight(0),
	tid(0),
	type_(0),
	handle_(0),
	version_(0),
	checkVersion_(0)
{

}

void Monitor::Watcher::trigger(uint32_t type, uint32_t handle)
{
	ATOM_SET(&type_, type);
	ATOM_SET(&handle_, handle);
	ATOM_INC(&version_);	//原子自增
}

bool Monitor::Watcher::check()
{
	if ((ATOM_GET(&version_) == ATOM_GET(&checkVersion_)) && ATOM_GET(&handle_)) {
		return true;
	} else {
		ATOM_SET(&checkVersion_, version_);
		return false;
	}
}
uint32_t Monitor::Watcher::type()
{
	return ATOM_GET(&type_);
}

uint32_t Monitor::Watcher::handle()
{
	return ATOM_GET(&handle_);
}
uint32_t Monitor::Watcher::version()
{
	return ATOM_GET(&version_);
}



Monitor::Monitor() :
	watchers_()
{

}
void Monitor::setWatcherNum(int n)
{
	watchers_.clear();
	for (int i=0; i<n; i++) {
		watchers_.push_back(WatcherPtr(new Watcher));
	}
}
Monitor::WatcherPtr Monitor::watcher(int i)
{
	assert(i < watchers_.size() && i >= 0);
	return watchers_[i];
}
int Monitor::size()
{
	return static_cast<int>(watchers_.size());
}
