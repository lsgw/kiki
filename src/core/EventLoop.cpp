#include "EventLoop.h"
#include "Channel.h"
#include "Context.h"
#include "Monitor.h"
#include "sockets.h"
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <vector>
#include <memory>


EventLoop::EventLoop() :
	looping_(false),
	quit_(false),
	selector_(new Selector()),
	spinports_(),
	ports_()
{
	fprintf(stderr, "EventLoop create\n");
	if (::socketpair(AF_UNIX, SOCK_STREAM, 0, wakeupFd_) < 0) {
		fprintf(stderr,"Failed in socketpair\n");
		abort();
	}
	sockets::setNonBlockAndCloseOnExec(wakeupFd_[0]);
	sockets::setNonBlockAndCloseOnExec(wakeupFd_[1]);
	using namespace std::placeholders;
	wakeupChannel_.reset(new Channel(NULL, wakeupFd_[0]));
	wakeupChannel_->setEventCallback(std::bind(&EventLoop::wakeupRead, this, _1, _2));
	wakeupChannel_->enableReading();
	updateChannel(wakeupChannel_.get());
}

EventLoop::~EventLoop()
{
	fprintf(stderr, "EventLoop destry\n");
	wakeupChannel_->disableAll();
	updateChannel(wakeupChannel_.get());
	::close(wakeupFd_[0]);
	::close(wakeupFd_[1]);
}
void EventLoop::quit()
{
	quit_ = true;
	wakeup();
}

#define noWait false
#define doWait true
void EventLoop::loop(Monitor::WatcherPtr watcher)
{
	assert(!looping_);
	looping_ = true;
	while (!quit_) {
		while (dispatch(watcher)) {
			checkIo(noWait);
		}
		checkIo(doWait);
	}
	looping_ = false;
}

bool EventLoop::dispatch(Monitor::WatcherPtr watcher)
{
	std::list<ContextPtr> ports;
	{
		SpinLockGuard lock(spinports_);
		ports.swap(ports_);
	}
	
	if (ports.empty()) {
		// printf("EventLoop::dispatch() empty\n");
		return false;
	}
	// printf("EventLoop::dispatch() port\n");
	for (auto ctx : ports) {
		ctx->dispatch(watcher);
	}
	return true;
}

void EventLoop::checkIo(bool wait)
{
	std::vector<Channel*> activeChannels;
	uint64_t pollTime = selector_->poll(wait? ms:0, &activeChannels);
	for (auto ch : activeChannels) {
		ch->handleEvent(pollTime);
	}
}

void EventLoop::queueInLoop(const ContextPtr& ctx)
{
	//printf("vvvvv1\n");
	{
		SpinLockGuard lock(spinports_);
		ports_.push_front(ctx);
	}
	//printf("sssss1\n");
	wakeup();
	//printf("uuuuu1\n");
}

void EventLoop::updateChannel(Channel* ch)
{
	selector_->updateChannel(ch);
}

uint32_t EventLoop::size()
{
	SpinLockGuard lock(spinports_);
	return static_cast<uint32_t>(ports_.size());
}

void EventLoop::wakeup()
{
	uint64_t one = 1;
	ssize_t n = sockets::write(wakeupFd_[1], &one, sizeof one);
	if (n != sizeof one) {
		fprintf(stderr, "EventLoop::wakeup() writes %zd bytes instead of 8\n", n);
	}
}
void EventLoop::wakeupRead(int fd, uint64_t polltime)
{
	uint64_t one;
	ssize_t n = sockets::read(wakeupFd_[0], &one, sizeof(one));
	if (n != sizeof one) {
		fprintf(stderr, "EventLoop::wakeupRead() reads %zd bytes instead of 8\n", n);
	}
}