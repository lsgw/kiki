#ifndef CHANNEL_H
#define CHANNEL_H

#include "noncopyable.h"
#include <stdint.h>
#include <functional>
#include <memory>


class Context;
class Channel;
using ChannelPtr = std::shared_ptr<Channel>;

class Channel : public noncopyable, public std::enable_shared_from_this<Channel> {
	using EventCallback = std::function<void(int fd, uint64_t polltime)>;
public:
	Channel(Context* context, int fd);
	int      fd() const { return fd_; }

	int event() const { return events_; }
	int revent() const { return revents_; }
	
	void setEventCallback(const EventCallback& cb) { eventCallback_ = cb; }
	void setEvent(int e) { events_ = e; }
	void setRevent(int e) { revents_ = e; }
	
	ChannelPtr enableReading();
	ChannelPtr enableWriting();
	ChannelPtr disableReading();
	ChannelPtr disableWriting();
	ChannelPtr disableAll();

	void update();

	bool isReading() const { return events_ & kReadEvent; }
	bool isWriting() const { return events_ & kWriteEvent; }
	bool isNoneEvent() const { return events_ == kNoneEvent; }

	bool hasReadEvent() const { return revents_ & kReadEvent; }
	bool hasWriteEvent() const { return revents_ & kWriteEvent; }
	bool hasErrorEvent() const { return revents_ & kErrorEvent; }

	void handleEvent(uint64_t polltime);

private:
	Context* context_;
	int      fd_;      // 文件描述符
	int      events_;  // 注册的事件
	int      revents_; // 发生的事件
	
	EventCallback eventCallback_;
public:                                 
	static const int kNoneEvent  = 0B000; // 无任何事件
	static const int kReadEvent  = 0B001; // 可读事件
	static const int kWriteEvent = 0B010; // 可写事件
	static const int kErrorEvent = 0B100; // 错误事件
};

#endif