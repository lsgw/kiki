#ifndef WORKERLOOP_H
#define WORKERLOOP_H

#include "noncopyable.h"
#include "SpinLock.h"
#include "MutexLock.h"
#include "Condition.h"
#include "Message.h"
#include "Monitor.h"
#include <memory>
#include <list>

class WorkerLoop : public noncopyable {
public:
	WorkerLoop();
	~WorkerLoop();

	void quit();
	void loop(Monitor::WatcherPtr watcher);
	void queueInLoop(const ContextPtr& ctx);
	void wakeup();
	uint32_t size();
private:
	bool dispatch(Monitor::WatcherPtr watcher);
	ContextPtr pop();

	bool looping_;
	bool quit_;

	SpinLock spin_;
	SpinLock spinPending_;
	std::list<ContextPtr> contexts_;
	std::list<ContextPtr> contextsPending_;

	MutexLock notsleepmutex_;
	Condition notsleepcondition_;
};

#endif