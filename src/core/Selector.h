#ifndef SELECTOR_H
#define SELECTOR_H

#include "noncopyable.h"
#include <vector>
#include <map>

class Channel;

#ifdef __MACH__
class Selector : noncopyable {
public:
	Selector();
	~Selector();
	
	uint64_t poll(int waitms, std::vector<Channel*>* activeChannels);
	void updateChannel(Channel* ch);
private:
	int kfd_;
	std::vector<struct kevent> events_;
};
#else
class Selector : noncopyable {
public:
	Selector();
	~Selector();
	
	uint64_t poll(int waitms, std::vector<Channel*>* activeChannels);
	void updateChannel(Channel* ch);
private:
	bool hasChannel(Channel* channel) const;
	int efd_;
	std::vector<struct epoll_event> events_;
	std::map<int, Channel*> channels_;
};
#endif

#endif