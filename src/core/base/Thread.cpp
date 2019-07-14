#include "Thread.h"
#include <unistd.h>
#include <assert.h>
#include <type_traits>
#include <sys/syscall.h>
#ifndef __MACH__
#include <sys/prctl.h>
#include <linux/unistd.h>
#endif


namespace detail
{

#ifdef __MACH__
pid_t gettid()
{
	return pthread_mach_thread_np(pthread_self());
}
#else
pid_t gettid()
{
	return static_cast<pid_t>(::syscall(SYS_gettid));
}
#endif

void afterFork()
{
	CurrentThread::t_cachedTid = 0;
	CurrentThread::t_threadName = "main";
	CurrentThread::tid();
	// no need to call pthread_atfork(NULL, NULL, &afterFork);
}

class ThreadNameInitializer {
public:
	ThreadNameInitializer()
	{
		CurrentThread::t_threadName = "main";
		CurrentThread::tid();
		pthread_atfork(NULL, NULL, &afterFork);
	}
};

ThreadNameInitializer init;

}

namespace CurrentThread
{
	
__thread int t_cachedTid = 0;
__thread char t_tidString[32];
__thread int t_tidStringLength = 6;
__thread const char* t_threadName = "unknown";
static_assert(std::is_same<int, pid_t>::value, "pid_t should be int");
void cacheTid()
{
	if (t_cachedTid == 0)
	{
		t_cachedTid = detail::gettid();
		t_tidStringLength = snprintf(t_tidString, sizeof t_tidString, "%5d ", t_cachedTid);
	}
}

bool isMainThread()
{
	return tid() == ::getpid();
}


}

struct ThreadData
{
	using ThreadFunc = Thread::ThreadFunc;
	ThreadFunc func_;
	std::string name_;
	pid_t* tid_;
	CountDownLatch* latch_;

	ThreadData(ThreadFunc func, const std::string& name, pid_t* tid, CountDownLatch* latch) :
		func_(std::move(func)),
		name_(name),
		tid_(tid),
		latch_(latch)
	{

	}

	void runInThread()
	{
		*tid_ = CurrentThread::tid();
		tid_ = NULL;
		latch_->countDown();
    	latch_ = NULL;

		CurrentThread::t_threadName = name_.empty() ? "muduoThread" : name_.c_str();
#ifndef __MACH__
    	::prctl(PR_SET_NAME, CurrentThread::t_threadName);
#endif
		try {
			func_();
			CurrentThread::t_threadName = "finished";
		} catch (const std::exception& ex) {
			CurrentThread::t_threadName = "crashed";
			fprintf(stderr, "exception caught in Thread %s\n", name_.c_str());
			fprintf(stderr, "reason: %s\n", ex.what());
			abort();
		} catch (...) {
			CurrentThread::t_threadName = "crashed";
			fprintf(stderr, "unknown exception caught in Thread %s\n", name_.c_str());
			throw; // rethrow
		}
	}
};

void* startThread(void* obj)
{
	ThreadData* data = static_cast<ThreadData*>(obj);
	data->runInThread();
	delete data;
	return NULL;
}



Thread::Thread(ThreadFunc func, const std::string& n) :
	started_(false),
	joined_(false),
	detached_(false),
	pthreadId_(0),
	tid_(0),
	func_(std::move(func)),
	name_(n),
	latch_(1)
{
}

Thread::~Thread()
{
	if (started_ && !joined_)
	{
		pthread_detach(pthreadId_);
	}
}

Thread& Thread::start()
{
	assert(!started_);
	started_ = true;
	// FIXME: move(func_)
	ThreadData* data = new ThreadData(func_, name_, &tid_, &latch_);
	if (pthread_create(&pthreadId_, NULL, &startThread, data)) {
		started_ = false;
		delete data; // or no delete?
		fprintf(stderr, "uFailed in pthread_create");
		abort();
	} else {
		latch_.wait();
		assert(tid_ > 0);
	}
	return *this;
}

int Thread::join()
{
	assert(started_);
	assert(!joined_);
	assert(!detached_);
	joined_ = true;
	return pthread_join(pthreadId_, NULL);
}

int Thread::detach()
{
	assert(started_);
	assert(!joined_);
	assert(!detached_);
	detached_ = true;
	return pthread_detach(pthreadId_);
}