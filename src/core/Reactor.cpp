#include "Reactor.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <string>
#include <functional>
#include <vector>

#define DEFAULT_SLOT_SIZE  8
#define HANDLE_MASK        0xffffffff

Reactor::Reactor() :
	quit_(false),
	started_(false),
	next_(0),
	workerLoopThreadNum_(0),
	eventLoopThreadNum_(0),
	modules_(new ModuleList()),
	timer_(new Timer()),
	monitor_(new Monitor()),
	loops_(),
	workerLoop_(new WorkerLoop()),
	threads_(),
	lock_(),
	index_(1),
	size_(DEFAULT_SLOT_SIZE),
	slot_(DEFAULT_SLOT_SIZE, ContextPtr()),
	env_()
{
	timer_->setCallback([this](uint32_t type, uint32_t handle, uint32_t session, uint64_t time){
		std::vector<char> data(sizeof(type)+sizeof(handle)+sizeof(session)+sizeof(time));
		char* p = &*data.begin();
		::memcpy(p, &type, sizeof(type));
		p += sizeof(type);
		::memcpy(p, &handle, sizeof(handle));
		p += sizeof(handle);
		::memcpy(p, &session, sizeof(session));
		p += sizeof(session);
		::memcpy(p, &time, sizeof(time));
		auto msg  = std::make_shared<Message>();
		msg->type = MSG_TYPE_TIME;
		msg->data.swap(data);
		this->postMessage(handle, msg);
	});
}

Reactor::~Reactor()
{
	if (!quit_) {
		quit();
	}
	for (auto& thread : threads_) {
		thread->join();
	}
}

void Reactor::start(const Env& env)
{
	loadenv(env);

	// 每个线程都有一个权重 权重为-1只处理一条消息 权重为0就将此服务的所有消息处理完 权重大于1就处理服务的部分消息
	static int weight[] = {
		0, -1,  0, -1,  0, -1,  0, -1,
		1,  1,  1,  1,  1,  1,  1,  1,
		2,  2,  2,  2,  2,  2,  2,  2,
		3,  3,  3,  3,  3,  3,  3,  3,
	};
	assert(eventLoopThreadNum_ > 0);
	assert(workerLoopThreadNum_ > 0);
	monitor_->setWatcherNum(eventLoopThreadNum_ + workerLoopThreadNum_);
	for (int i=0; i<eventLoopThreadNum_; i++) {
		monitor_->watcher(i)->weight = 0;
	}
	int base = eventLoopThreadNum_;
	for (int i=0; i<workerLoopThreadNum_; i++) {
		if (i < static_cast<int>(sizeof(weight)/sizeof(weight[0]))) {
			monitor_->watcher(base + i)->weight = weight[i];
		} else {
			monitor_->watcher(base + i)->weight = 0;
		}
	}

	for (int i=0; i<eventLoopThreadNum_; i++) {
		loops_.push_back(std::unique_ptr<EventLoop>(new EventLoop()));
	}
	for (int i=0; i<eventLoopThreadNum_; i++) {
		std::string name = std::string("EventLoop-") + std::to_string(i); 
		threads_.push_back(std::unique_ptr<Thread>(new Thread(std::bind(&Reactor::threadEvent, this, i), name)));
	}
	
	for (int i=0; i<workerLoopThreadNum_; i++) {
		std::string name = std::string("WorkerLoop-") + std::to_string(i); 
		threads_.push_back(std::unique_ptr<Thread>(new Thread(std::bind(&Reactor::threadWorker, this, i), name)));
	}
	threads_.push_back(std::unique_ptr<Thread>(new Thread(std::bind(&Reactor::threadTimer, this), std::string("TimerLoop-0"))));
	
	for (auto& thread : threads_) {
		thread->start();
		//printf("pid=%#x, tid=%#x\n", thread->pthreadId(), thread->tid());
	}

	std::vector<char> v(env_.entry.args.begin(), env_.entry.args.end());
	v.push_back('\0');
	uint32_t handle = newcontext(env_.entry.main, 0, MSG_TYPE_JSON, std::move(v));
	assert(handle > 0);
	
	started_ = true;
}

void Reactor::loop()
{
	while (!quit_) {
		for (int i=0; i<monitor_->size(); i++) {
			if (monitor_->watcher(i)->check()) {
				std::vector<char> data(512);
				uint32_t handle = 0;
				uint32_t level  = 4;
				char* p = &*data.begin();
				::memcpy(p, &handle, sizeof(handle));
				p += sizeof(handle);
				::memcpy(p, &level, sizeof(level));
				p += sizeof(level);
				snprintf(p, 128, "A message type[%d] in handle[%d] maybe in an endless loop (version = %d)", monitor_->watcher(i)->type(), monitor_->watcher(i)->handle(), monitor_->watcher(i)->version());
				auto msg  = std::make_shared<Message>();
				msg->type = MSG_TYPE_LOG;
				msg->data.swap(data);
				postMessage(env_.logger, msg);
			}
		}
		for (int i=0; i<5; i++) {
			sleep(1);
		}
	}
}

void Reactor::threadEvent(int idx)
{
	int base = 0;
	loops_[idx]->loop(monitor_->watcher(base + idx));
}

void Reactor::threadWorker(int idx)
{
	int base = eventLoopThreadNum_;
	workerLoop_->loop(monitor_->watcher(base + idx));
}
void Reactor::threadTimer()
{
	timer_->loop();
}

void Reactor::loadenv(const Env& env)
{
	eventLoopThreadNum_  = env.thread.event;
	workerLoopThreadNum_ = env.thread.worker;
	modules_->setModulePath(env.module.path);
	env_ = env;
}

EventLoop* Reactor::getNextLoop()
{
	assert(started_);
	assert(loops_.size() > 0);

	EventLoop* loop = NULL;
	if (!loops_.empty()) {
		// round-robin
		loop = loops_[next_].get();
		++next_;
		if (static_cast<size_t>(next_) >= loops_.size()) {
			next_ = 0;
		}
	}

	assert(loop != NULL);
	return loop;
}
void Reactor::quit()
{
	assert(started_);
	assert(loops_.size() > 0);
	for (auto& loop : loops_) {
		loop->quit();
	}
	workerLoop_->quit();
	timer_->quit();
	quit_ = true;
}

uint32_t Reactor::newcontext(const std::string& module, uint32_t handle, uint32_t type, std::vector<char>&& data)
{
	ModulePtr m = modules_->query(module);
	if (!m) {
		fprintf(stderr, "query %s fail\n", module.c_str());
		return 0;
	}
	void* actor = m->create();
	if (!actor) {
		fprintf(stderr, "create %s fail\n", module.c_str());
		return 0;
	}

	using namespace std::placeholders;
	ContextPtr ctx(new Context(NULL, actor));
	ctx->setHandle(registerContext(ctx));
	ctx->setModule(m);
	ctx->setOwner(handle);
	ctx->setRunInLoopCallback(std::bind(&WorkerLoop::queueInLoop, workerLoop_.get(), _1));
	ctx->setReleaseCallback(std::bind(&Reactor::unregisterContext, this, _1));
	ctx->setNewcontextCallback(std::bind(&Reactor::newcontext, this, _1, _2, _3, _4));
	ctx->setNewportCallback(std::bind(&Reactor::newport, this, _1, _2, _3, _4));
	ctx->setSendmsgCallback(std::bind(&Reactor::postMessage, this, _1, _2));
	ctx->setTimeoutCallback(std::bind(&Timer::timeout, timer_.get(), _1, _2, _3, _4));
	ctx->setAbortCallback(std::bind(&Reactor::quit, this));
	ctx->setReloadCallback(std::bind(&ModuleList::reload, modules_.get(), _1));
	ctx->setGetEnvCallback([this]()->Env&{ return env_; });
	ctx->setGetQLenCallback(std::bind(&WorkerLoop::size, workerLoop_.get()));
	ctx->setGetRLenCallback(std::bind(&Reactor::rlen, this));
	ctx->setProfile(env_.profile);
	ctx->recv(ctx->makeMessage(type, std::forward<std::vector<char>>(data)));

	return ctx->handle();
}

uint32_t Reactor::newport(const std::string& driver, uint32_t handle, uint32_t type, std::vector<char>&& data)
{
	ModulePtr m = modules_->query(driver);
	if (!m) {
		fprintf(stderr, "new %s fail\n", driver.c_str());
		return 0;
	}
	void* actor = m->create();
	if (!actor) {
		fprintf(stderr, "create %s fail\n", driver.c_str());
		return 0;
	}

	using namespace std::placeholders;
	ContextPtr ctx(new Context(getNextLoop(), actor));
	ctx->setHandle(registerContext(ctx));
	ctx->setModule(m);
	ctx->setOwner(handle);
	ctx->setRunInLoopCallback(std::bind(&EventLoop::queueInLoop, ctx->loop(), _1));
	ctx->setReleaseCallback(std::bind(&Reactor::unregisterContext, this, _1));
	ctx->setNewportCallback(std::bind(&Reactor::newport, this, _1, _2, _3, _4));
	ctx->setSendmsgCallback(std::bind(&Reactor::postMessage, this, _1, _2));
	ctx->setTimeoutCallback(std::bind(&Timer::timeout, timer_.get(), _1, _2, _3, _4));
	ctx->setAbortCallback(std::bind(&Reactor::quit, this));
	ctx->setReloadCallback(std::bind(&ModuleList::reload, modules_.get(), _1));
	ctx->setGetEnvCallback([this]()->Env&{ return env_; });
	ctx->setGetQLenCallback(std::bind(&EventLoop::size, ctx->loop()));
	ctx->setGetRLenCallback(std::bind(&Reactor::rlen, this));
	ctx->setProfile(env_.profile);
	ctx->recv(ctx->makeMessage(type, std::forward<std::vector<char>>(data)));

	return ctx->handle();
}

bool Reactor::postMessage(uint32_t handle, MessagePtr message)
{
	ContextPtr ctx = grab(handle);
	if (ctx) {
		ctx->recv(message);
		return true;
	} else {
		return false;
	}
}


bool Reactor::unregisterContext(uint32_t handle)
{
	bool ret = false;

	lock_.wrlock();

	uint32_t hash = handle & (size_ - 1);
	ContextPtr ctx = slot_[hash];

	if (ctx && ctx->handle() == handle) {
		slot_[hash] = ContextPtr();
		ret = true;
	}

	lock_.unlock();

	return ret;
}

uint32_t Reactor::registerContext(ContextPtr ctx)
{
	lock_.wrlock();

	while (true) {
		for(uint32_t i=0; i<size_; i++) {
			uint32_t handle = (i + index_) & HANDLE_MASK;
			uint32_t hash = handle & (size_ - 1);
			if (!slot_[hash] && handle>0) {
				slot_[hash] = ctx;
				index_ = handle + 1;
				lock_.unlock();
				return handle;
			}
		}
		assert((size_*2 - 1) < HANDLE_MASK);		
		std::vector<ContextPtr> slot(size_*2, ContextPtr());
		for (uint32_t i=0; i<size_; i++) {
			uint32_t hash = slot_[i]->handle() & (size_*2-1);
			assert(slot_[i].get() != NULL);
			slot[hash] = slot_[i];
		}

		slot_.swap(slot);
		size_ *= 2;
	}

	return 0;
}

ContextPtr Reactor::grab(uint32_t handle)
{
	ContextPtr result;
	
	lock_.rdlock();
	uint32_t hash = handle & (size_ - 1);
	if (slot_[hash] && slot_[hash]->handle() == handle) {
		result = slot_[hash];
	}
	lock_.unlock();

	return result;
}

uint32_t Reactor::rlen()
{
	lock_.rdlock();
	uint32_t len = static_cast<uint32_t>(slot_.size());
	uint32_t n = 0;
	for (uint32_t i=0; i<len; i++) {
		if (slot_[i]) {
			n = n + 1;
		}
	}
	lock_.unlock();
	return n;
}


