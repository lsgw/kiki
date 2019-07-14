#ifndef CONTEXT_H
#define CONTEXT_H

#include "noncopyable.h"
#include "SpinLock.h"
#include "Channel.h"
#include "Module.h"
#include "Monitor.h"
#include "Message.h"
#include "Env.h"
#include <memory>
#include <functional>
#include <map>
#include <list>

class EventLoop;
class Context : public noncopyable, public std::enable_shared_from_this<Context> {
	using RunInLoopCallback  = std::function<void(const ContextPtr& ctx)>;
	using ReleaseCallback    = std::function<bool(uint32_t handle)>;
	using NewcontextCallback = std::function<uint32_t(const std::string& module, uint32_t handle, uint32_t type, std::vector<char>&& data)>;
	using NewportCallback    = std::function<uint32_t(const std::string& driver, uint32_t handle, uint32_t type, std::vector<char>&& data)>;
	using SendmsgCallback    = std::function<bool(uint32_t handle, MessagePtr message)>;
	using TimeoutCallback    = std::function<void(uint32_t type, uint32_t context, uint32_t session, uint64_t time)>;
	using AbortCallback      = std::function<void(void)>;
	using ReloadCallback     = std::function<bool(const std::string& module)>;
	using GetEnvCallback     = std::function<Env&(void)>;
	using GetQLenCallback    = std::function<uint32_t(void)>;
	using GetRLenCallback    = std::function<uint32_t(void)>;
public:
	Context(EventLoop* loop, void* actor);
	~Context();
	
	EventLoop* loop() const { return loop_; }
	void*      actor() const { return actor_; }
	uint32_t   handle() const { return handle_; }
	ModulePtr  module() const { return module_; }
	uint32_t   owner() { uint32_t o = 0; {SpinLockGuard lock(ownerlock_); o = owner_; } return o; }
	
	void setHandle(uint32_t  handle) { handle_ = handle; }
	void setModule(ModulePtr module) { module_ = module; }
	void setOwner(uint32_t handle) { SpinLockGuard lock(ownerlock_); owner_ = handle; }
	void setRunInLoopCallback(const RunInLoopCallback& cb) { runInLoopCallback_ = cb; }
	void setReleaseCallback(const ReleaseCallback& cb) { releaseCallback_ = cb; }
	void setNewcontextCallback(const NewcontextCallback& cb) { newcontextCallback_ = cb; }
	void setNewportCallback(const NewportCallback& cb) { newportCallback_ = cb; }
	void setSendmsgCallback(const SendmsgCallback& cb) { sendmsgCallback_ = cb; }
	void setTimeoutCallback(const TimeoutCallback& cb) { timeoutCallback_ = cb; }
	void setAbortCallback(const AbortCallback& cb) { abortCallback_ = cb; }
	void setReloadCallback(const ReloadCallback& cb) { reloadCallback_ = cb; }
	void setGetEnvCallback(const GetEnvCallback& cb) { getEnvCallback_ = cb; }
	void setGetQLenCallback(const GetQLenCallback& cb) { getQLenCallback_ = cb; }
	void setGetRLenCallback(const GetRLenCallback& cb) { getRLenCallback_ = cb; }

	uint32_t newsession();
	uint32_t newcontext(const std::string& module, uint32_t handle, uint32_t type, std::vector<char>&& data);
	uint32_t newport(const std::string& driver, uint32_t handle, uint32_t type, std::vector<char>&& data);

	void recv(MessagePtr message);
	bool send(uint32_t handle, MessagePtr message);
	void timeout(uint32_t session, uint64_t ms);
	bool reload(const std::string& module);
	Env& env();
	
	void dispatch(Monitor::WatcherPtr watcher);

	MessagePtr makeMessage() const;
	MessagePtr makeMessage(uint32_t type) const;
	MessagePtr makeMessage(uint32_t type, std::vector<char>&& data) const;
	void exit();
	void abort();
	void yield();

	uint32_t rlen();
	uint32_t qlen();
	uint32_t mlen();
	uint64_t cpuCost() const;
	uint32_t messageCount();
	void setProfile(bool on);
	bool getProfile() const;

	ChannelPtr channel(int fd);
	void registerEvent(ChannelPtr ch);
	void triggerEvent(int fd, uint64_t polltime);
private:
	bool isSystemMessageType(uint32_t type);

	EventLoop*      loop_;
	void*           actor_;
	uint32_t        handle_;
	ModulePtr       module_;
	uint32_t        session_;
	SpinLock        ownerlock_;
	uint32_t        owner_;

	RunInLoopCallback  runInLoopCallback_;
	ReleaseCallback    releaseCallback_;
	NewcontextCallback newcontextCallback_;
	NewportCallback    newportCallback_;
	SendmsgCallback    sendmsgCallback_;
	TimeoutCallback    timeoutCallback_;
	AbortCallback      abortCallback_;
	ReloadCallback     reloadCallback_;
	GetEnvCallback     getEnvCallback_;
	GetQLenCallback    getQLenCallback_;
	GetRLenCallback    getRLenCallback_;

	SpinLock              spinmailbox_;   // 保护 global_, mailboxPending_, mailboxSystem_, messageCount_ 4个成员
	bool                  global_;
	std::list<MessagePtr> mailboxPending_;
	std::list<MessagePtr> mailboxSystem_; // 这个邮箱的消息类型必须是能一次性处理完成的(处理过程中不能yield)，保存休息类型为: EXIT, DEBUG
	std::list<MessagePtr> mailboxHold_;

	uint64_t cpuCost_;
	uint32_t messageCount_;
	bool     profile_;

	std::map<int, ChannelPtr> channels_; // 用于注册事件和保存发生的事件
};

#endif
