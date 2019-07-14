#ifndef THREAD_H
#define THREAD_H

#include "noncopyable.h"
#include "CountDownLatch.h"
#include <string>
#include <functional>
#include <pthread.h>


namespace CurrentThread {


// internal
extern __thread int t_cachedTid;
extern __thread char t_tidString[32];
extern __thread int t_tidStringLength;
extern __thread const char* t_threadName;

void cacheTid();
bool isMainThread();

inline int tid()
{
	if (__builtin_expect(t_cachedTid == 0, 0))
	{
		cacheTid();
	}
	return t_cachedTid;
}

inline const char* tidString() // for logging
{
	return t_tidString;
}

inline int tidStringLength() // for logging
{
	return t_tidStringLength;
}

inline const char* name()
{
	return t_threadName;
}


}

class Thread : noncopyable {
public:
	using ThreadFunc = std::function<void()>;

	explicit Thread(ThreadFunc, const std::string& name = std::string());
	// FIXME: make it movable in C++11
	~Thread();

	Thread& start();
	int join(); // return pthread_join()
	int detach();

	bool started() const { return started_; }
	pthread_t pthreadId() const { return pthreadId_; }
	pid_t tid() const { return tid_; }
	const std::string& name() const { return name_; }

private:
	bool       started_;
	bool       joined_;
	bool       detached_;
	pthread_t  pthreadId_;
	pid_t      tid_;
	ThreadFunc func_;
	std::string     name_;
	CountDownLatch latch_;
};

#endif