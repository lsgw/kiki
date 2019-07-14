#ifndef CONDITION_H
#define CONDITION_H

#include "noncopyable.h"
#include "MutexLock.h"
#include <pthread.h>

class Condition : noncopyable {
public:
	explicit Condition(MutexLock& mutex) : mutex_(mutex)
	{
		MCHECK(pthread_cond_init(&pcond_, NULL));
	}
	~Condition()
	{
		MCHECK(pthread_cond_destroy(&pcond_));
	}

	void wait()
	{
		MCHECK(pthread_cond_wait(&pcond_, mutex_.getPthreadMutex()));
	}

	void notify()
	{
		MCHECK(pthread_cond_signal(&pcond_));
	}
	void notifyAll()
	{
		MCHECK(pthread_cond_broadcast(&pcond_));
	}

private:
	MutexLock& mutex_;
	pthread_cond_t pcond_;
};


#endif