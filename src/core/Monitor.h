#ifndef MONITOR_H
#define MONITOR_H

#include "noncopyable.h"
#include "Condition.h"
#include <stdint.h>
#include <unistd.h>
#include <vector>
#include <memory>

class Monitor : public noncopyable {
public:
	class Watcher : public noncopyable {
	public:
		Watcher();
		void trigger(uint32_t type, uint32_t handle);
		bool check();
		
		uint32_t type();
		uint32_t handle();

		uint32_t version();
		
		int32_t  weight;
		pid_t    tid;

	private:
		uint32_t  type_;
		uint32_t  handle_;

		uint32_t  version_;
		uint32_t  checkVersion_;
	};
	using WatcherPtr = std::shared_ptr<Watcher>;
	
	Monitor();
	void setWatcherNum(int n);
	WatcherPtr watcher(int i);
	int size();
private:
	std::vector<WatcherPtr> watchers_;
};



#endif