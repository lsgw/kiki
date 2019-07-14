#ifndef TIMER_H
#define TIMER_H

#include "SpinLock.h"
#include <stdint.h>
#include <stdlib.h>
#include <vector>
#include <functional>
class Timer {
	using Callback = std::function<void(uint32_t type, uint32_t context, uint32_t session, uint64_t time)>;
public:
	Timer();
	~Timer();
	void loop();
	void quit();
	void timeout(uint32_t type, uint32_t context, uint32_t session, uint64_t time);
	void setCallback(const Callback& cb) { callback = cb; }
private:
	struct Node {
		uint32_t type;
		uint32_t context;
		uint32_t session;
		uint64_t time;
		uint64_t expire;
		Node* next;
	};
	struct List {
		Node  head;
		Node* tail;
	};
	void update();
	void add(Node* node);
	void shift();
	void execute();
	
	bool looping_;
	bool quit_;

	SpinLock spin;
	std::vector<List> tv[5];
	uint64_t interval;      //us
	uint64_t current;
	uint64_t jiffies;

	Callback callback;
};

#endif