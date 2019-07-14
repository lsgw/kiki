#ifndef MUTEXLOCK_H
#define MUTEXLOCK_H

#include "noncopyable.h"
#include <assert.h>
#include <pthread.h>


#define MCHECK(ret) ({__typeof__(ret) errnum=(ret); \
                      assert(errnum==0); (void)errnum;})

class MutexLock : public noncopyable {
public:
	MutexLock() : holder_(0)
	{
		assert(holder_ == 0);
		MCHECK(pthread_mutex_init(&mutex_, NULL));
	}

	~MutexLock()
	{
		assert(holder_ == 0);
		MCHECK(pthread_mutex_destroy(&mutex_));
	}

	void lock()
	{
		holder_ = 1;
		MCHECK(pthread_mutex_lock(&mutex_));
	}
	bool trylock()
	{
		return pthread_mutex_trylock(&mutex_) == 0;
	}

	void unlock()
	{
		holder_ = 0;
		MCHECK(pthread_mutex_unlock(&mutex_));
	}

	pthread_mutex_t* getPthreadMutex()
	{
		return &mutex_;
	}

private:
	pthread_mutex_t mutex_;
	pid_t holder_;
};

class MutexLockGuard : public noncopyable {
public:
	explicit MutexLockGuard(MutexLock& mutex) : mutex_(mutex)
	{
		mutex_.lock();
	}
	~MutexLockGuard()
	{
		mutex_.unlock();
	}

private:
	MutexLock& mutex_;
};

#endif