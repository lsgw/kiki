#ifndef REACTOR_H
#define REACTOR_H

#include "noncopyable.h"
#include "EventLoop.h"
#include "WorkerLoop.h"
#include "Thread.h"
#include "RWLock.h"
#include "Context.h"
#include "Message.h"
#include "Monitor.h"
#include "Timer.h"
#include "Env.h"
#include <vector>
#include <memory>

class Reactor : public noncopyable {
public:
	Reactor();
	~Reactor();

	void start(const Env& env);
	void loop();
	void quit();

	uint32_t newcontext(const std::string& module, uint32_t handle, uint32_t type, std::vector<char>&& data);
	uint32_t newport(const std::string& driver, uint32_t handle, uint32_t type, std::vector<char>&& data);
	bool postMessage(uint32_t handle, MessagePtr message);
	
	bool unregisterContext(uint32_t handle);
	uint32_t registerContext(ContextPtr ctx);
	ContextPtr grab(uint32_t handle);
	uint32_t rlen();
private:
	void loadenv(const Env& env);
	EventLoop* getNextLoop();
	void threadEvent(int idx);
	void threadWorker(int idx);
	void threadTimer();

	bool quit_;
	bool started_;
	int next_;
	int workerLoopThreadNum_;
	int eventLoopThreadNum_;
	std::unique_ptr<ModuleList> modules_;
	std::unique_ptr<Timer> timer_;
	std::unique_ptr<Monitor> monitor_;
	std::vector<std::unique_ptr<EventLoop>> loops_;
	std::unique_ptr<WorkerLoop> workerLoop_;
	std::vector<std::unique_ptr<Thread>> threads_;

	RWLock   lock_;
	uint32_t index_;
	uint32_t size_;
	std::vector<ContextPtr> slot_;

	Env env_;
};

#endif