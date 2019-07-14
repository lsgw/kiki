#ifndef ACTOR_H
#define ACTOR_H

#include "noncopyable.h"
#include "Context.h"
#include "Message.h"
#include <functional>

template <typename T>
class Actor : public noncopyable {
	using MessageCallback = std::function<bool(ContextPtr,MessagePtr&)>;
public:
	Actor()
	{ 
		messageCallback_ = std::bind(&Actor::callback_init, this, std::placeholders::_1, std::placeholders::_2);
	}
	virtual ~Actor()
	{

	}

	virtual void init(ContextPtr ctx, MessagePtr& message)
	{

	}
	virtual void release(ContextPtr ctx, MessagePtr& message)
	{

	}
	virtual bool receive(ContextPtr ctx, MessagePtr& message)
	{
		return true;
	}
	
	
	bool callback(ContextPtr ctx, MessagePtr& message)
	{
		return messageCallback_(ctx, message);
	}

private:
	bool callback_init(ContextPtr ctx, MessagePtr& message)
	{
		init(ctx, message);
		messageCallback_ = std::bind(&Actor::callback_recv, this, std::placeholders::_1, std::placeholders::_2);
		return true;
	}
	bool callback_recv(ContextPtr ctx, MessagePtr& message)
	{
		return receive(ctx, message);
	}
	MessageCallback messageCallback_;
};

#define module(T)\
class T;\
extern "C" {\
\
void* actor_create()\
{\
	void* p = static_cast<void*>(new T());\
	return p;\
}\
\
bool actor_callback(ContextPtr ctx, void* actor, MessagePtr message)\
{\
	T* p = static_cast<T*>(actor);\
	return p->callback(ctx, message);\
}\
\
void actor_release(ContextPtr ctx, void* actor, MessagePtr message)\
{\
	T* p = static_cast<T*>(actor);\
	p->release(ctx, message);\
	delete p;\
}\
\
}\

#endif