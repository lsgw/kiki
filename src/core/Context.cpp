#include "Context.h"
#include "EventLoop.h"
#include <sys/time.h>
#include <string.h>

uint64_t current_thread_time()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	uint64_t cp = (uint64_t)tv.tv_sec * 1000000 + (uint64_t)tv.tv_usec;
	return cp;
}

Context::Context(EventLoop* loop, void* actor) :
	loop_(loop),
	actor_(actor),
	handle_(0),
	module_(),
	session_(0),
	ownerlock_(),
	owner_(0),
	runInLoopCallback_(),
	releaseCallback_(),
	newcontextCallback_(),
	newportCallback_(),
	sendmsgCallback_(),
	timeoutCallback_(),
	abortCallback_(),
	reloadCallback_(),
	spinmailbox_(),
	global_(false),
	mailboxPending_(),
	mailboxSystem_(),
	mailboxHold_(),
	cpuCost_(0),
	messageCount_(0),
	profile_(false)
{
	//fprintf(stderr, "new Context\n");
}
Context::~Context()
{
	//fprintf(stderr, "del Context\n");
	if (loop_ != NULL) {
		std::vector<int> fds(channels_.size(), -1);
		int i = 0;
		for (auto m : channels_) {
			fds[i++] = m.first;
		}
		for (auto fd : fds) {
			channel(fd)->disableAll()->update();
			::close(fd);
			// printf("close fd = %d\n", fd);
		}
	}
}

uint32_t Context::newsession()
{
	session_++;
	if (session_ == 0) {
		session_ = 1;
	}
	return session_;
}

uint32_t Context::newcontext(const std::string& module, uint32_t handle, uint32_t type, std::vector<char>&& data)
{
	return newcontextCallback_(module, handle, type, std::forward<std::vector<char>>(data));
}

uint32_t Context::newport(const std::string& driver, uint32_t handle, uint32_t type, std::vector<char>&& data)
{
	return newportCallback_(driver, handle, type, std::forward<std::vector<char>>(data));
}

void Context::recv(MessagePtr message)
{
	SpinLockGuard lock(spinmailbox_);
	if (isSystemMessageType(message->type)) {
		mailboxSystem_.push_front(message);
	} else {
		mailboxPending_.push_front(message);
	}
	if (!global_) {
		global_ = true;
		runInLoopCallback_(shared_from_this());
	}
	messageCount_++;
}
bool Context::isSystemMessageType(uint32_t type)
{
	switch (type) {
	case MSG_TYPE_EXIT:
		return true;
	case MSG_TYPE_DEBUGS:
		return true;
	default:
		return false;
	}
}
bool Context::send(uint32_t handle, MessagePtr message)
{
	return sendmsgCallback_(handle, message);
}
void Context::timeout(uint32_t session, uint64_t ms)
{
	uint32_t type = loop_==NULL? 0:1;
	timeoutCallback_(type, handle_, session, ms);
}
bool Context::reload(const std::string& module)
{
	return reloadCallback_(module);
}
Env& Context::env()
{
	return getEnvCallback_();
}

void Context::dispatch(Monitor::WatcherPtr watcher)
{
	int n = 1;
	MessagePtr message;
	for (int i=0; i<n; i++) {
		{
			SpinLockGuard lock(spinmailbox_);
			if (!mailboxSystem_.empty()) {
				MessagePtr hold = mailboxSystem_.back();
				mailboxSystem_.pop_back();
				mailboxPending_.push_back(hold);
			}
			if (i == 0) {
				if (watcher->weight >= 0) {
					n   = mailboxPending_.size();
					n >>= watcher->weight;
				}
			}
			assert(mailboxPending_.size() > 0);
			message = mailboxPending_.back();
			mailboxPending_.pop_back();
		}
		
		if (message->type == MSG_TYPE_EXIT) {
			module_->release(shared_from_this(), actor_, message);
			bool ret = releaseCallback_(handle_);
			assert(ret);
			return ;
		}
		bool match = false;
		if (profile_) {
			uint64_t cpuStart = current_thread_time();
			watcher->trigger(message->type, handle_);
			match = module_->callback(shared_from_this(), actor_, message);
			watcher->trigger(0, 0);
			uint64_t cpuEnd   = current_thread_time();
			cpuCost_ += cpuEnd - cpuStart;
		} else {
			watcher->trigger(message->type, handle_);
			match = module_->callback(shared_from_this(), actor_, message);
			watcher->trigger(0, 0);
		}
		if (!match) {
			mailboxHold_.push_front(message);
		} else {
			while (!mailboxHold_.empty()) {
				MessagePtr hold = mailboxHold_.front();
				mailboxHold_.pop_front();
				{
					SpinLockGuard lock(spinmailbox_);
					mailboxPending_.push_back(hold);
				}
			}
		}
	}
	{
		SpinLockGuard lock(spinmailbox_);
		if (mailboxPending_.size() + mailboxSystem_.size() > 0) {
			global_ = true;
			runInLoopCallback_(shared_from_this());
		} else {
			global_ = false;
		}
	}
}


MessagePtr Context::makeMessage() const
{
	auto msg = std::make_shared<Message>();
	return msg;
}
MessagePtr Context::makeMessage(uint32_t type) const
{
	auto msg = std::make_shared<Message>();
	msg->type = type;
	return msg;
}
MessagePtr Context::makeMessage(uint32_t type, std::vector<char>&& data) const
{
	auto msg  = std::make_shared<Message>();
	msg->type = type;
	msg->data.swap(data);
	return msg;
}
void Context::exit()
{
	recv(makeMessage(MSG_TYPE_EXIT));
}
void Context::abort()
{
	abortCallback_();
}
void Context::yield()
{
	SpinLockGuard lock(spinmailbox_);
	mailboxPending_.push_back(makeMessage());
}

uint32_t Context::rlen()
{
	return getRLenCallback_();
}
uint32_t Context::qlen()
{
	return getQLenCallback_();
}
uint32_t Context::mlen()
{
	SpinLockGuard lock(spinmailbox_);
	return static_cast<uint32_t>(mailboxPending_.size() + mailboxSystem_.size() + mailboxHold_.size());
}

uint64_t Context::cpuCost() const
{
	return cpuCost_;
}
uint32_t Context::messageCount()
{
	SpinLockGuard lock(spinmailbox_);
	return messageCount_;
}
void Context::setProfile(bool on)
{
	profile_ = on;
}
bool Context::getProfile() const
{
	return profile_;
}

ChannelPtr Context::channel(int fd)
{
	assert(loop_ != NULL);
	ChannelPtr ch(new Channel(this, fd));
	auto it = channels_.find(fd);
	if (it != channels_.end()) {
		ch->setEvent(it->second->event());
	}
	return ch;
}
void Context::registerEvent(ChannelPtr ch)
{
	assert(loop_ != NULL);
	using namespace std::placeholders;
	int  fd = ch->fd();
	auto it = channels_.find(fd);
	if ( it == channels_.end()) {
		if (!ch->isNoneEvent()) {
			ch->setEventCallback(std::bind(&Context::triggerEvent, this, _1, _2));
			channels_[fd] = ch;
			loop_->updateChannel(channels_[fd].get());
		}
	} else {
		if (!ch->isNoneEvent()) {
			channels_[fd]->setEvent(ch->event());
			loop_->updateChannel(channels_[fd].get());
		} else {
			channels_[fd]->setEvent(ch->event());
			loop_->updateChannel(channels_[fd].get());
			channels_.erase(fd);
		}
	}
}

/// IO Event Protocol
/// +-------------------+-------------------+------------------+
/// |     polltime      |     Event Type    |     Event fd     |  
/// +-------------------+-------------------+------------------+
/// | -->  uint64_t <-- | -->  uint32_t <-- | -->   int    <-- |
///
///                          1 --> read
///                          2 --> write
///                          3 --> error
#define IOREAD  1
#define IOWRITE 2
#define IOERROR 3
void Context::triggerEvent(int fd, uint64_t polltime)
{
	// printf("handle=%d, fd = %d, polltime = %llu, rlen=%d, qlen=%d, mlen=%d\n", handle_, fd, polltime,  rlen(),  qlen(),  mlen());
	auto pack = [](uint64_t eventtime, uint32_t eventtype, int eventfd) {
		std::vector<char> data(sizeof(eventtime)+sizeof(eventtype)+sizeof(eventfd));
		char* p = &*data.begin();
		::memcpy(p, &eventtime, sizeof(eventtime));
		p += sizeof(eventtime);
		::memcpy(p, &eventtype, sizeof(eventtype));
		p += sizeof(eventtype);
		::memcpy(p, &eventfd, sizeof(eventfd));
		return data;
	};
	assert(channels_.find(fd) != channels_.end());
	auto& ch = channels_[fd];
	assert(fd == ch->fd());
	if (ch->hasReadEvent()) {
		recv(makeMessage(MSG_TYPE_EVENT, pack(polltime, IOREAD,  ch->fd())));
	}
	if (ch->hasWriteEvent()) {
		recv(makeMessage(MSG_TYPE_EVENT, pack(polltime, IOWRITE, ch->fd())));
	}
	if (ch->hasErrorEvent()) {
		recv(makeMessage(MSG_TYPE_EVENT, pack(polltime, IOERROR, ch->fd())));
	}
	ch->setRevent(Channel::kNoneEvent);
}