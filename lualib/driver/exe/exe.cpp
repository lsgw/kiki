#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"  
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "Logger/Log.h"
#include "sockets.h"
#include "Buffer.h"
#include "Actor.h"
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <assert.h>
#include <errno.h>
#include <dirent.h>
#include <sys/time.h>
#include <functional>
#include <string>
#include <vector>
#include <set>
#include <map>



using namespace std::placeholders;
using namespace rapidjson;

// ┌──────────┬───────────────┬──────────┬───────────┐
//   文件描述符     POSIX名称      stdio流       用途      
// ├──────────┼───────────────┼──────────┼───────────┤
// │    0     │ STDIN_FILENO  │  stdin   │   标准输入   
// │    1     │ STDOUT_FILENO │  stdout  │   标准输出   
// │    2     │ STDERR_FILENO │  stderr  │   标准错误   
// └──────────┴───────────────┴──────────┴───────────┘
static uint64_t gettimetoms()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	uint64_t cp = (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec/1000;
	return cp;
}

static bool isAccess(const std::string& path)
{
	if (access(path.c_str(), X_OK) == 0) {
		return true;
	} else {
		return false;
	}
}
static bool isExist(int fd, int array[], int n)
{
	for (int i=0; i<n; i++) {
		if (array[i] == fd) {
			return true;
		}
	}
	return false;
}
static int closeOpenFd(int unclosefd[], int n)
{
	struct dirent *entry, _entry;
	int retval, rewind, fd;
	DIR* dir = opendir("/dev/fd");
	if (dir == NULL) {
		return -1;
	}

	rewind = 0;
	while (1) {
		retval = readdir_r(dir, &_entry, &entry);
		if (retval != 0) {
			errno = -retval;
			retval = -1;
			break;
		}
		if (entry == NULL) {
			if (!rewind) {
				break;
			}
			rewinddir(dir);
			rewind = 0;
			continue;
		}
		if (entry->d_name[0] == '.') {
			continue;
		}
		fd = atoi(entry->d_name);
		if (dirfd(dir) == fd || isExist(fd, unclosefd, n)) {
			continue;
		}

		retval = close(fd);
		if (retval != 0) {
			break;
		}
		rewind = 1;
	}

	closedir(dir);

	return retval;
}
static int spawn(const std::string& path, const std::vector<std::string>& ss, pid_t* exepid, int* exefd)
{
	int fd = *exefd;

	if (!isAccess(path)) {
		return 0;
	}
	int comfd[2] = { 0 };
	int ret = ::socketpair(AF_UNIX, SOCK_STREAM, 0, comfd);
	if (ret < 0) {
		return 0;
	}

	pid_t pid = ::fork();
	if (pid < 0) {
		::close(comfd[0]);
		::close(comfd[1]);
		return 0;
	}

	if (pid > 0) {
		*exepid = pid;
		*exefd = comfd[0];
		::close(comfd[1]);
		return 1;
	}

	if (fd < 0) {
		if (::dup2(comfd[1], STDIN_FILENO)  != STDIN_FILENO) {
			::exit(0);
		}
		if (::dup2(comfd[1], STDOUT_FILENO) != STDOUT_FILENO) {
			::exit(0);
		}
		if (::dup2(comfd[1], STDERR_FILENO) != STDERR_FILENO) {
			::exit(0);
		}
	} else {
		if (::dup2(comfd[1], fd) != fd) {
			::exit(0);
		}
	}
	int unclosefd[4] = {0, 1, 2, fd};
	closeOpenFd(unclosefd, 4);

	char** argv = (char**)malloc(sizeof(char**) * (ss.size() + 1));
	::memset(argv, 0, sizeof(char**) * (ss.size() + 1));
	for (std::vector<std::string>::size_type i=0; i<ss.size(); i++) {
		char* data = (char*)malloc(ss[i].size()+1);
		::memset(data, 0, ss[i].size()+1);
		::memcpy(data, ss[i].data(), ss[i].size());
		argv[i] = data;
	}
	::execve(path.c_str(), argv, NULL);
	::exit(0); // 运行到这里，肯定有问题

	return 1;
}

void signalAction(ContextPtr ctx, const std::string& status, pid_t pid)
{
	assert(status == "subscribe" || status == "unsubscribe");

	Document document;
	Document::AllocatorType& allocator = document.GetAllocator();
	Value csigt;
	Value csigs;
	Value array(kArrayType);
	csigt.SetString(status.data(), status.size(), allocator);
	csigs.SetString("SIGCHLD", strlen("SIGCHLD"), allocator);
	array.PushBack("cast",        allocator);
	array.PushBack(ctx->handle(), allocator);
	array.PushBack("ref",         allocator);
	array.PushBack(csigt,         allocator);
	array.PushBack(ctx->handle(), allocator);
	array.PushBack(csigs,         allocator);
	array.PushBack(pid,       allocator);

	StringBuffer buffer;
	Writer<rapidjson::StringBuffer> writer(buffer);
	array.Accept(writer);
	std::string args = buffer.GetString();
	ctx->send(ctx->env().signal,  ctx->makeMessage(MSG_TYPE_JSON, std::vector<char>(args.begin(), args.end())));
}


class exe : public Actor<exe> {
public:
	void init(ContextPtr ctx, MessagePtr& message) override
	{
		commands_.insert({"start",    std::bind(&exe::start,    this, _1, _2, _3, _4, _5, _6)});
		commands_.insert({"info",     std::bind(&exe::info,     this, _1, _2, _3, _4, _5, _6)});
		commands_.insert({"recv",     std::bind(&exe::recv,     this, _1, _2, _3, _4, _5, _6)});
		commands_.insert({"send",     std::bind(&exe::send,     this, _1, _2, _3, _4, _5, _6)});
		commands_.insert({"signal",   std::bind(&exe::signal,   this, _1, _2, _3, _4, _5, _6)});
		commands_.insert({"shutdown", std::bind(&exe::shutdown, this, _1, _2, _3, _4, _5, _6)});
		commands_.insert({"close",    std::bind(&exe::close,    this, _1, _2, _3, _4, _5, _6)});

		log.ctx    = ctx.get();
		log.handle = ctx->env().logger;

		state_       = kOpening;
		timerActive_ = false;

		exesig_  = false;
		exepid_  = -1;
		exefd_   = -1;
		timeout_ = -1;
		active_  = false;

		bRevent_ = false;
		bWevent_ = false;


		time_t now = 0;
		char timebuf[32];
		struct tm tm;
		now = time(NULL);
		localtime_r(&now, &tm);
		// gmtime_r(&now, &tm);
		strftime(timebuf, sizeof timebuf, "%Y-%m-%d %H:%M:%S", &tm);
		infoStartT_ = timebuf;
		infoCountR_ = 0;
		infoCountW_ = 0;

		assert(message->type == MSG_TYPE_JSON);
		auto& data = message->data;
		Document document;
		document.Parse(static_cast<char*>(&*data.begin()), data.size());
		assert(document.IsArray());
		assert(document.Size() == 1);

		const Value& opts = document[0];
		assert(opts.IsObject());
		assert(opts.HasMember("path"));
		path_ = opts["path"].GetString();
		argv_.push_back(path_);
		if (opts.HasMember("argv")) {
			assert(opts["argv"].IsArray());
			const Value& array = opts["argv"];
			for (int i=0; i<array.Size(); i++) {
				assert(array[i].IsString());
				argv_.push_back(array[i].GetString());
			}
		}
		if (opts.HasMember("fd")) {
			assert(opts["fd"].IsInt());
			exefd_ = opts["fd"].GetInt();
			assert(exefd_ >= 0);
		}
		if (opts.HasMember("timeout")) {
			assert(opts["timeout"].IsInt());
			timeout_ = opts["timeout"].GetInt();
			assert(timeout_ > 0);
		}
		if (opts.HasMember("active")) {
			assert(opts["active"].IsBool());
			active_ = opts["active"].GetBool();
		}
	}
	void release(ContextPtr ctx, MessagePtr& message) override
	{
		Document document;
		Document::AllocatorType& allocator = document.GetAllocator();
		
		Value array(kArrayType);
		array.PushBack(Value("cast",  allocator).Move(),   allocator);
		array.PushBack(ctx->handle(), allocator);
		array.PushBack(Value("ref",   allocator).Move(),   allocator);
		array.PushBack(Value("exit",  allocator).Move(),   allocator);
		array.PushBack(ctx->handle(), allocator);
		StringBuffer buffer;
		Writer<rapidjson::StringBuffer> writer(buffer);
		array.Accept(writer);
		std::string args = buffer.GetString();
		ctx->send(ctx->env().launcher, ctx->makeMessage(MSG_TYPE_JSON, std::vector<char>(args.begin(), args.end())));

		if (exefd_ >= 0) {
			ctx->channel(exefd_)->disableAll()->update();
			::close(exefd_);
			exefd_ = -1;
		}
		if (exepid_ >= 0 && !exesig_) {
			::signalAction(ctx, "unsubscribe", exepid_);
			::kill(exepid_, SIGKILL);
			exepid_ = -1;
			exesig_ = true;
		}
		state_ = kClosed;
	}

	bool receive(ContextPtr ctx, MessagePtr& message) override
	{
		if (message->type == MSG_TYPE_JSON) {
			auto& data = message->data;
			Document document;
			document.Parse(static_cast<char*>(&*data.begin()), data.size());
			Document::AllocatorType& allocator = document.GetAllocator();
			assert(document.IsArray());

			Value array = document.GetArray();
			assert(array.Size() >= 4);
			assert(array[0].IsString()); // pattern: call, cast
			assert(array[1].IsInt());    // source
			assert(array[2].IsString()); // ref
			assert(array[3].IsString()); // function name

			std::string pattern  = array[0].GetString();
			uint32_t    source   = array[1].GetInt();
			std::string ref      = array[2].GetString();
			std::string funcname = array[3].GetString();

			auto it = commands_.find(funcname);
			assert(it != commands_.end());
			auto func = it->second;

			Value param(kArrayType);     // function param
			for (uint32_t i=4; i<array.Size(); i++) {
				param.PushBack(array[i], allocator);
			}
			func(ctx, pattern, source, ref, document, param);
		} else if (message->type == MSG_TYPE_EVENT) {
			uint64_t eventtime =  0;
			uint32_t eventtype =  0;
			int      eventfd   = -1;

			assert(message->data.size() == (sizeof(eventtime)+sizeof(eventtype)+sizeof(eventfd)));
			char* p = message->data.data();
			::memcpy(&eventtime, p, sizeof(eventtime));
			p += sizeof(eventtime);
			::memcpy(&eventtype, p, sizeof(eventtype));
			p += sizeof(eventtype);
			::memcpy(&eventfd,   p, sizeof(eventfd));
			
			#define IOREAD  1
			#define IOWRITE 2
			#define IOERROR 3
			assert(eventfd == exefd_);
			if (eventtype < IOREAD || eventtype > IOERROR) {
				assert(0);
			} else if (eventtype == IOREAD) {
				onRead(ctx, eventtime, eventtype, eventfd);
			} else if (eventtype == IOWRITE) {
				onWrite(ctx, eventtime, eventtype, eventfd);
			} else if (eventtype == IOERROR) {
				onError(ctx, eventtime, eventtype, eventfd);
			}
		} else if (message->type == MSG_TYPE_TIME) {
			uint32_t type    = 0;
			uint32_t handle  = 0;
			uint32_t session = 0;
			uint64_t time    = 0;

			assert(message->data.size() == (sizeof(type)+sizeof(handle)+sizeof(session)+sizeof(time)));
			char* p = message->data.data();
			::memcpy(&type, p, sizeof(type));
			p += sizeof(type);
			::memcpy(&handle, p, sizeof(handle));
			p += sizeof(handle);
			::memcpy(&session, p, sizeof(session));
			p += sizeof(session);
			::memcpy(&time, p, sizeof(time));

			assert(ctx->handle() == handle);
			assert(type==1 && ctx->loop()!=NULL);
			onTimer(ctx, type, handle, session, time);
		} else {
			assert(0);
		}
		return true;
	}

	void start(ContextPtr ctx, const std::string& pattern, uint32_t source, const std::string& ref, Document& document, const Value& param)
	{
		assert(pattern == "call");
		assert(exepid_ < 0);
		int ok = spawn(path_, argv_, &exepid_, &exefd_);
		if (ok) {
			signalAction(ctx, "subscribe", exepid_);
			ctx->channel(exefd_)->enableReading()->update();
			bRevent_ = true;
			state_ = kOpened;
		}
		Document::AllocatorType& allocator = document.GetAllocator();
		Value refs;
		Value rets(kArrayType);
		refs.SetString(ref.data(), ref.size(), allocator);
		rets.PushBack("resp",                  allocator);
		rets.PushBack(ctx->handle(),           allocator);
		rets.PushBack(refs,                    allocator);
		rets.PushBack(ok,                      allocator);
		rets.PushBack(exepid_,                 allocator);
		rets.PushBack(exefd_,                  allocator);
		
		StringBuffer buffer;
		Writer<rapidjson::StringBuffer> writer(buffer);
		rets.Accept(writer);
		std::string rss = buffer.GetString();
		ctx->send(source, ctx->makeMessage(MSG_TYPE_JSON, std::vector<char>(rss.begin(), rss.end())));

		LOG_DEBUG << "start ok=" << ok;
	}
	void info(ContextPtr ctx, const std::string& pattern, uint32_t source, const std::string& ref, Document& document, const Value& param)
	{
		assert(pattern == "call");
		assert(exepid_ >= 0);
		assert(exefd_ >= 0);
		assert(exesig_ == false);

		Document::AllocatorType& allocator = document.GetAllocator();
		Value refs;
		Value info(kObjectType);
		Value rets(kArrayType);
		refs.SetString(ref.data(), ref.size(), allocator);
		info.AddMember(Value("path", allocator).Move(), Value(path_.c_str(), allocator).Move(), allocator);
		info.AddMember(Value("time", allocator).Move(), Value(infoStartT_.c_str(), allocator).Move(), allocator);
		info.AddMember(Value("hrcount", allocator).Move(), infoCountR_, allocator);
		info.AddMember(Value("hwcount", allocator).Move(), infoCountW_, allocator);
		info.AddMember(Value("crcount", allocator).Move(), static_cast<uint32_t>(inputBuffer_.readableBytes()), allocator);
		info.AddMember(Value("cwcount", allocator).Move(), static_cast<uint32_t>(outputBuffer_.readableBytes()), allocator);
		rets.PushBack("resp",           allocator);
		rets.PushBack(ctx->handle(),    allocator);
		rets.PushBack(refs,             allocator);
		rets.PushBack(info,             allocator);
		
		StringBuffer buffer;
		Writer<rapidjson::StringBuffer> writer(buffer);
		rets.Accept(writer);
		std::string rss = buffer.GetString();
		ctx->send(source, ctx->makeMessage(MSG_TYPE_JSON, std::vector<char>(rss.begin(), rss.end())));
	}
	void recv(ContextPtr ctx, const std::string& pattern, uint32_t source, const std::string& ref, Document& document, const Value& param)
	{
		assert(pattern == "call");
		assert(source == ctx->owner());
		if (active_) {
			onResult(ctx, "resp", source, ref, -1);
			return;
		}
		assert(param.IsArray());
		ssize_t cbytes = inputBuffer_.readableBytes();
		ssize_t rbytes = -1;
		ssize_t tbytes = -1;
		
		if (param.Size() == 1) {
			assert(param[0].IsInt());
			rbytes = param[0].GetInt();
		}
		if (cbytes > 0) {
			rbytes = rbytes>0? rbytes : cbytes; 
			tbytes = rbytes<cbytes? rbytes : cbytes;
			onResult(ctx, "resp", source, ref, tbytes);
			return;
		}
		if (cbytes == 0 && !bRevent_) {
			ctx->channel(exefd_)->enableReading()->update();
			bRevent_ = true;
		}
		if (timeout_ > 0) {
			if (!timerActive_) {
				timerActive_ = true;
				uint32_t session = ctx->newsession();
				ctx->timeout(session, timeout_ * 1000);	
			}
			request_.push_front({pattern, source, ref, rbytes, gettimetoms() + timeout_ * 1000});
		} else {
			request_.push_front({pattern, source, ref, rbytes, 0});
		}
	}
	void send(ContextPtr ctx, const std::string& pattern, uint32_t source, const std::string& ref, Document& document, const Value& param)
	{
		assert(param.IsArray());
		assert(param.Size() == 1);
		assert(param[0].IsString());
		const void* data = static_cast<const void*>(param[0].GetString());
		size_t len = static_cast<size_t>(param[0].GetStringLength());
		//printf("port handle = %d, send total message = %zd\n", ctx->handle(), len);
		ssize_t nwrote = 0;
		size_t remaining = len;
		bool faultError = false;
		if (state_ != kOpened) {
			return;
		}
		
		// if no thing in output queue, try writing directly
		if (!ctx->channel(exefd_)->isWriting() && outputBuffer_.readableBytes() == 0) {
			nwrote = sockets::write(exefd_, data, len);
			//printf("port handle = %d, write n bytes = %zd\n", ctx->handle(), nwrote);
			if (nwrote >= 0) {
				infoCountW_ += nwrote;
				remaining = len - nwrote;
			} else { // nwrote < 0
				nwrote = 0;
				if (errno != EWOULDBLOCK) {
					if (errno == EPIPE || errno == ECONNRESET) { // FIXME: any others?
						faultError = true;
					}
				}
			}
		}

		assert(remaining <= len);
		if (!faultError && remaining > 0) {
			// size_t oldLen = outputBuffer_.readableBytes();
			outputBuffer_.append(static_cast<const char*>(data)+nwrote, remaining);
			if (!ctx->channel(exefd_)->isWriting()) {
				//printf("port handle = %d, enableWriting\n", ctx->handle());
				ctx->channel(exefd_)->enableWriting()->update();
			}
		}
		//printf("port handle = %d, send save  message = %zd\n", ctx->handle(), outputBuffer_.readableBytes());
	}
	void signal(ContextPtr ctx, const std::string& pattern, uint32_t source, const std::string& ref, Document& document, const Value& param)
	{
		signalAction(ctx, "unsubscribe", exepid_);
		exesig_ = true;
		assert(param.IsArray());
		assert(param.Size() == 1);
		assert(param[0].IsString());
		std::string signame = param[0].GetString();
		assert(signame == "SIGCHLD");
		LOG_DEBUG << "sig = " << signame;
	}

	void shutdown(ContextPtr ctx, const std::string& pattern, uint32_t source, const std::string& ref, Document& document, const Value& param)
	{
		assert(pattern == "cast");
		assert(state_ != kClosed);
		if (!ctx->channel(exefd_)->isWriting()) {
			sockets::shutdownWrite(exefd_);
		}
		state_ = kClosing;
		LOG_DEBUG << "shutdown";
	}
	
	void close(ContextPtr ctx, const std::string& pattern, uint32_t source, const std::string& ref, Document& document, const Value& param)
	{
		assert(pattern == "cast");
		assert(state_ != kClosed);
		ctx->exit();
		LOG_DEBUG << "exit";
	}




	void onRead(ContextPtr ctx, uint64_t eventtime, uint32_t eventtype, int eventfd)
	{
		int savedErrno = 0;
		ssize_t n = inputBuffer_.readFd(eventfd, &savedErrno);
		if (n == 0) {
			bRevent_ = false;
			ctx->channel(exefd_)->disableReading()->update();
			if (state_ == kClosing) {
				Document document;
				close(ctx, "cast", 0, "ref", document, Value(kArrayType));
			}
		}
		if (active_) {
			if (n >= 0) {
				onResult(ctx, "cast", ctx->owner(), "ref", n);
			}
		} else {
			if (inputBuffer_.readableBytes() >= kCacheRLimit) {
				bRevent_ = false;
				ctx->channel(exefd_)->disableReading()->update();
			}
			if (n >= 0 && request_.size() > 0) {
				auto& req = request_.back();
				assert(req.pattern == "call");
				assert(req.expire == 0);
				onResult(ctx, "resp", req.source, req.ref, n);
				request_.pop_back();
			}
		}
		if (n > 0) {
			infoCountR_ += n;
		}
		LOG_DEBUG << "onRead n=" << n;
	}
	void onWrite(ContextPtr ctx, uint64_t eventtime, uint32_t eventtype, int eventfd)
	{
		if (ctx->channel(exefd_)->isWriting()) {
			ssize_t n = sockets::write(exefd_, outputBuffer_.peek(), outputBuffer_.readableBytes());
			if (n > 0) {
				infoCountW_ += n;
				outputBuffer_.retrieve(n);
				if (outputBuffer_.readableBytes() == 0) {
					ctx->channel(exefd_)->disableWriting()->update();
					if (state_ == kClosing) {
						sockets::shutdownWrite(exefd_);
					}
				}
			} else {
				char t_errnobuf[512] = {'\0'};
				strerror_r(errno, t_errnobuf, sizeof t_errnobuf);
				fprintf(stderr, "port handle = %d, fd = %d, errno = %d, write error = %s\n", ctx->handle(), exefd_, errno, t_errnobuf);
				ctx->channel(exefd_)->disableWriting()->update();
			}
		} else {
			//LOG_TRACE << "Connection fd = " << channel_->fd() << " is down, no more writing";
		}
	}
	void onError(ContextPtr ctx, uint64_t eventtime, uint32_t eventtype, int eventfd)
	{
		
	}
	void onTimer(ContextPtr ctx, uint32_t type, uint32_t handle, uint32_t session, uint64_t ms)
	{
		assert(timeout_ > 0);
		uint64_t nowms = gettimetoms();
		std::list<Request> request;
		request.swap(request_);

		for (auto it=request.begin(); it!=request.end(); it++) {
			if (nowms >= it->expire) {
				assert(it->pattern == "call");
				onResult(ctx, "resp", it->source, it->ref, -1);
			} else {
				request_.push_back(*it);
			}
		}
		if (request_.size() > 0) {
			uint32_t session = ctx->newsession();
			ctx->timeout(session, timeout_ * 1000);	
		} else {
			timerActive_ = false;
		}
	}

	void onResult(ContextPtr ctx, const std::string& pattern, uint32_t source, const std::string& ref, ssize_t nbyte)
	{
		Document document;
		Document::AllocatorType& allocator = document.GetAllocator();
		Value patt;
		Value refs;
		Value exec;
		Value path;
		Value msgs;
		
		patt.SetString(pattern.data(), pattern.size(),    allocator);
		refs.SetString(ref.data(), ref.size(),            allocator);
		exec.SetString("exe", strlen("exe"),              allocator);
		path.SetString(path_.data(), path_.size(),        allocator);
		msgs.SetString(inputBuffer_.peek(), nbyte,        allocator);
		inputBuffer_.retrieve(nbyte);

		Value rets(kArrayType);
		rets.PushBack(patt,          allocator);
		rets.PushBack(ctx->handle(), allocator);
		rets.PushBack(refs,          allocator);
		if (pattern == "cast") {
			rets.PushBack(exec,          allocator);
			rets.PushBack(path,          allocator);
		}
		if (nbyte >= 0) {
			rets.PushBack(msgs,          allocator);
		}

		StringBuffer buffer;
		Writer<rapidjson::StringBuffer> writer(buffer);
		rets.Accept(writer);
		std::string args = buffer.GetString();
		ctx->send(source,  ctx->makeMessage(MSG_TYPE_JSON, std::vector<char>(args.begin(), args.end())));
	}

private:
	struct Request {
		std::string pattern;
		uint32_t source;
		std::string ref;
		ssize_t rbytes;
		uint64_t expire;
	};
	enum State {kOpening, kOpened, kClosing, kClosed};
	State state_;

	std::list<Request> request_;
	bool  timerActive_;
	bool  exesig_;
	pid_t exepid_;
	int   exefd_;
	int   timeout_;
	bool  active_;
	std::string path_;
	std::vector<std::string> argv_;

	bool bRevent_;
	bool bWevent_;

	std::string infoStartT_;
	uint32_t    infoCountR_;
	uint32_t    infoCountW_;

	Buffer inputBuffer_;
	Buffer outputBuffer_;
	
	const static uint32_t kCacheRLimit = 1024 * 1024;
	const static uint32_t kCacheWLimit = 1024 * 1024;

	std::map<std::string, std::function<void(ContextPtr,std::string,uint32_t,std::string,rapidjson::Document&,const rapidjson::Value&)>> commands_;
	Log log;
};

module(exe)