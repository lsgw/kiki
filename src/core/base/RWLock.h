#ifndef RWLOCK_H
#define RWLOCK_H

#include "noncopyable.h"
#include <pthread.h>
#include <assert.h>

class RWLock : noncopyable {
public:
	RWLock() : holder_(0)
	{
		pthread_rwlock_init(&rwlock_, NULL);
	}
	~RWLock()
	{
		assert(holder_ == 0);
		pthread_rwlock_destroy(&rwlock_);
	}

	void rdlock()
	{
		holder_ = 1;
		pthread_rwlock_rdlock(&rwlock_);
	}
	void wrlock()
	{
		holder_ = 2;
		pthread_rwlock_wrlock(&rwlock_);
	}
	void unlock()
	{
		holder_ = 0;
		pthread_rwlock_unlock(&rwlock_);
	}
private:
	pthread_rwlock_t rwlock_;
	pid_t holder_;
};

#endif