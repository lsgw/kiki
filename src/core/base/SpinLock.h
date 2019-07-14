#ifndef SPINLOCK_H
#define SPINLOCK_H

#include "noncopyable.h"
class SpinLock : public noncopyable {
public:
	SpinLock() : lock_(0) { }
	~SpinLock() { (void) lock_; }
	void lock() { while (__sync_lock_test_and_set(&lock_,1)) { } }
	bool trylock() { return __sync_lock_test_and_set(&lock_,1) == 0; }
	void unlock() { __sync_lock_release(&lock_); }
private:
	int lock_;
};

class SpinLockGuard : public noncopyable {
public:
	explicit SpinLockGuard(SpinLock& spin) : spin_(spin)
	{
		spin_.lock();
	}
	~SpinLockGuard()
	{
		spin_.unlock();
	}

private:
	SpinLock& spin_;
};

#endif
