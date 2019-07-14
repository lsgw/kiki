#include "WorkerLoop.h"
#include "Context.h"

WorkerLoop::WorkerLoop() :
	looping_(false),
	quit_(false),
	spin_(),
	spinPending_(),
	contexts_(),
	contextsPending_(),
	notsleepmutex_(),
	notsleepcondition_(notsleepmutex_)
{

}

WorkerLoop::~WorkerLoop()
{
	if (!quit_) {
		quit();
	}
}

void WorkerLoop::quit()
{
	quit_ = true;
	notsleepcondition_.notifyAll();
}
void WorkerLoop::loop(Monitor::WatcherPtr watcher)
{
	looping_ = true;
	while (!quit_) {
		if (!dispatch(watcher)) {
			MutexLockGuard lock(notsleepmutex_);
			notsleepcondition_.wait();
		}
	}
	looping_ = false;
}

void WorkerLoop::queueInLoop(const ContextPtr& ctx)
{
	{
		SpinLockGuard lockPending(spinPending_);
		contextsPending_.push_front(ctx);
	}
	wakeup();
}

void WorkerLoop::wakeup()
{
	MutexLockGuard lock(notsleepmutex_);
	notsleepcondition_.notify();
}

uint32_t WorkerLoop::size()
{
	SpinLockGuard lock(spin_);
	SpinLockGuard lockPending(spinPending_);
	return  static_cast<uint32_t>(contexts_.size() + contextsPending_.size());
}

bool WorkerLoop::dispatch(Monitor::WatcherPtr watcher)
{
	auto ctx = pop();
	if (ctx) {
		ctx->dispatch(watcher);
		return true;
	} else {
		return false;
	}
}
ContextPtr WorkerLoop::pop()
{
	ContextPtr ctx;
	{
		SpinLockGuard lock(spin_);
		if (contexts_.empty()) {
			{
				SpinLockGuard lockPending(spinPending_);
				if (!contextsPending_.empty()) {
					contexts_.swap(contextsPending_);
				}
			}
			if (!contexts_.empty()) {
				ctx = contexts_.back();
				contexts_.pop_back();
			}
		} else {
			ctx = contexts_.back();
			contexts_.pop_back();
		}
	}
	return ctx;
}