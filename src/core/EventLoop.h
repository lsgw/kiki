#ifndef EVENTLOOP_H
#define EVENTLOOP_H

#include "noncopyable.h"
#include "Selector.h"
#include "SpinLock.h"
#include "Message.h"
#include "Monitor.h"
#include <memory>
#include <list>

class EventLoop : public noncopyable {
public:
	EventLoop();
	~EventLoop();

	void quit();
	void loop(Monitor::WatcherPtr watcher);
	void queueInLoop(const ContextPtr& ctx);

	void wakeup();
	void wakeupRead(int fd, uint64_t polltime);
	void updateChannel(Channel* ch);

	uint32_t size();
private:
	bool dispatch(Monitor::WatcherPtr watcher);
	void checkIo(bool wait);

	bool looping_;
	bool quit_;
	std::unique_ptr<Selector> selector_;
	
	SpinLock spinports_;
	std::list<ContextPtr> ports_;

	int wakeupFd_[2];
	std::shared_ptr<Channel> wakeupChannel_;
	static const int ms = 3600000;
};


#endif
