#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"  
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "sockets.h"
#include "Buffer.h"
#include "Logger/Log.h"
#include "Actor.h"
#include "HttpContext.h"
#include "HttpResponse.h"
#include <time.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <assert.h>
#include <errno.h>
#include <functional>
#include <memory>
#include <string>
#include <list>
#include <set>
#include <map>
#include <unordered_map>
#include <queue>

using namespace std::placeholders;
using namespace rapidjson;


class http : public Actor<http> {
public:
	void init(ContextPtr ctx, MessagePtr& message) override
	{
		log.ctx    = ctx.get();
		log.handle = ctx->env().logger;
		//log.locals = 1;

		time_t now = 0;
		char timebuf[32];
		struct tm tm;
		now = time(NULL);
		localtime_r(&now, &tm);
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
		assert(opts.HasMember("type"));
		assert(opts["type"].IsString());
		std::string httpType = opts["type"].GetString();
		if (httpType == "httpl") {
			httpType_ = Type::kHttpL;
			commands_.insert({"start",      std::bind(&http::lStart,      this, _1, _2, _3, _4, _5, _6)});
			commands_.insert({"info",       std::bind(&http::info,        this, _1, _2, _3, _4, _5, _6)});
			commands_.insert({"close",      std::bind(&http::lClose,      this, _1, _2, _3, _4, _5, _6)});
			onRead_  = std::bind(&http::onlRead,  this, _1, _2, _3, _4);
			onWrite_ = std::bind(&http::onlWrite, this, _1, _2, _3, _4);
			onError_ = std::bind(&http::onlError, this, _1, _2, _3, _4);
			onTimer_ = std::bind(&http::onlTimer, this, _1, _2, _3, _4, _5);
		} else if (httpType == "httpd") {
			httpType_ = Type::kHttpD;
			commands_.insert({"start",      std::bind(&http::dStart,      this, _1, _2, _3, _4, _5, _6)});
			commands_.insert({"info",       std::bind(&http::info,        this, _1, _2, _3, _4, _5, _6)});
			commands_.insert({"connection", std::bind(&http::dConnection, this, _1, _2, _3, _4, _5, _6)});
			commands_.insert({"recv",       std::bind(&http::dRead,       this, _1, _2, _3, _4, _5, _6)});
			commands_.insert({"send",       std::bind(&http::dWrite,      this, _1, _2, _3, _4, _5, _6)});
			commands_.insert({"close",      std::bind(&http::dClose,      this, _1, _2, _3, _4, _5, _6)});
			onRead_  = std::bind(&http::ondRead,  this, _1, _2, _3, _4);
			onWrite_ = std::bind(&http::ondWrite, this, _1, _2, _3, _4);
			onError_ = std::bind(&http::ondError, this, _1, _2, _3, _4);
			onTimer_ = std::bind(&http::ondTimer, this, _1, _2, _3, _4, _5);
		} else if (httpType == "httpc") {
			httpType_ = Type::kHttpC;
		} else {
			assert(0);
		}
		// listener
		master_ = 0;
		fd_ = -1;
		cur_ = 0;

		// httpd
		connsize_    = 1024;
		connindex_   = 1;
		connections_ = std::vector<std::shared_ptr<Connection>>(connsize_, std::shared_ptr<Connection>());
		
		LOG_DEBUG << "Launch " << httpType << " service";
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

		if (httpType_ == Type::kHttpL) {
			if (fd_ >= 0) {
				ctx->channel(fd_)->disableAll()->update();
				sockets::close(fd_);
			}
		} else if (httpType_ == Type::kHttpD) {
			for (auto conn : connections_) {
				ctx->channel(conn->fd)->disableAll()->update();
				sockets::close(conn->fd);
			}
		}
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
			if (eventtype == IOREAD) {
				onRead_(ctx, eventtime, eventtype, eventfd);
			} else if (eventtype == IOWRITE) {
				onWrite_(ctx, eventtime, eventtype, eventfd);
			} else if (eventtype == IOERROR) {
				onError_(ctx, eventtime, eventtype, eventfd);
			} else {
				assert(0);
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
			onTimer_(ctx, type, handle, session, time);
		} else {
			assert(0);
		}
		return true;
	}

	void info(ContextPtr ctx, const std::string& pattern, uint32_t source, const std::string& ref, Document& document, const Value& param)
	{
		assert(pattern == "call");
		Document::AllocatorType& allocator = document.GetAllocator();
	
		Value info(kObjectType);
		Value rets(kArrayType);
		std::string httptype;
		if (httpType_ == Type::kHttpL) {
			httptype = "http listener";
		} else if (httpType_ == Type::kHttpD) {
			httptype = "http handler";
			info.AddMember(Value("connections", allocator).Move(), static_cast<uint32_t>(connections_.size()), allocator);
		} else if (httpType_ == Type::kHttpC) {
			httptype = "http client";
		} else {
			assert(0);
		}
		info.AddMember(Value("httptype", allocator).Move(), Value(httptype.data(), httptype.size(), allocator).Move(), allocator);
		info.AddMember(Value("time", allocator).Move(), Value(infoStartT_.data(), infoStartT_.size(), allocator).Move(), allocator);
		info.AddMember(Value("rcount", allocator).Move(), infoCountR_, allocator);
		info.AddMember(Value("wcount", allocator).Move(), infoCountW_, allocator);
		rets.PushBack(info,             allocator);
		
		onResult(ctx, document, source, "resp", ctx->handle(), ref, rets);
	}

	void lStart(ContextPtr ctx, const std::string& pattern, uint32_t source, const std::string& ref, Document& document, const Value& param)
	{
		assert(pattern == "call");
		assert(param.IsArray());
		assert(param.Size() == 2);
		assert(param[0].IsObject());
		assert(param[1].IsArray());
		const Value& value = param[0];
		const Value& array = param[1];
		assert(array.Size() > 0);
		for (auto& m : array.GetArray()) {
			workers_.push_back(static_cast<uint32_t>(m.GetInt()));
			LOG_TRACE << "worker = " << m.GetInt();
		}

		assert(value.HasMember("ip"));
		assert(value.HasMember("port"));
		assert(value.HasMember("ipv6"));
		assert(value["ip"].IsString());
		assert(value["port"].IsInt());
		assert(value["ipv6"].IsBool());

		std::string ip = value["ip"].GetString();
		uint16_t port  = value["port"].GetInt();
		bool  ipv6     = value["ipv6"].GetBool();

		struct sockaddr_in6 addr6;
		struct sockaddr_in  addr4;
		struct sockaddr* addr = NULL;
		bzero(&addr6, sizeof(addr6));
		bzero(&addr4, sizeof(addr4));
		
		int ok = 0;
		int fd = sockets::createTcpNonblockingOrDie(ipv6? AF_INET6 : AF_INET);
		assert(fd >= 0);
		sockets::setReuseAddr(fd, 1);
		sockets::setReusePort(fd, 1);
		if (ipv6) {
			addr = sockets::fromIpPort(ip.c_str(), port, &addr6);
		} else {
			addr = sockets::fromIpPort(ip.c_str(), port, &addr4);
		}
		if ((ok = sockets::bind(fd, addr)) == 0) {
			 ok = sockets::listen(fd);
		}
		if (ok == 0) {
			fd_ = fd;
			ctx->channel(fd_)->enableReading()->update();
		} else {
			sockets::close(fd);
		}
		LOG_TRACE << "listen ok = " << ok << ",fd = " << fd_;

		Document::AllocatorType& allocator = document.GetAllocator();
	
		Value rets(kArrayType);
		rets.PushBack(ok<0? false : true, allocator);
		onResult(ctx, document, source, "resp", ctx->handle(), ref, rets);
	}
	
	void lClose(ContextPtr ctx, const std::string& pattern, uint32_t source, const std::string& ref, Document& document, const Value& param)
	{
		assert(pattern == "cast");
		ctx->exit();
		LOG_DEBUG << "exit";
	}
	void onlRead(ContextPtr ctx, uint64_t eventtime, uint32_t eventtype, int eventfd)
	{
		assert(fd_ == eventfd);
		struct sockaddr_in6 addr6;
		int connfd = sockets::accept(fd_, &addr6);
		//LOG_TRACE << "new connection fd=" << connfd;
		if (connfd >= 0) {
			infoCountR_ += 1;
			Document document;
			Document::AllocatorType& allocator = document.GetAllocator();
			Value value(kArrayType);
			value.PushBack(Value("connection",   allocator).Move(), allocator);
			value.PushBack(connfd, allocator);
			uint32_t destination = workers_[cur_];
			cur_++;
			if (static_cast<uint32_t>(cur_) >= workers_.size()) {
				cur_ = 0;
			}
			onResult(ctx, document, destination, "cast", ctx->handle(), "ref", value);
		}
	}
	void onlWrite(ContextPtr ctx, uint64_t eventtime, uint32_t eventtype, int eventfd)
	{
		assert(0);
	}
	void onlError(ContextPtr ctx, uint64_t eventtime, uint32_t eventtype, int eventfd)
	{
		assert(0);
	}
	void onlTimer(ContextPtr ctx, uint32_t type, uint32_t handle, uint32_t session, uint64_t ms)
	{
		assert(0);
	}











	void dStart(ContextPtr ctx, const std::string& pattern, uint32_t source, const std::string& ref, Document& document, const Value& param)
	{
		assert(pattern == "call");
		assert(param.IsArray());
		assert(param.Size() == 1);
		assert(param[0].IsInt());
		master_ = param[0].GetInt();
		LOG_TRACE << "master = " << master_;

		Document::AllocatorType& allocator = document.GetAllocator();
		Value rets(kArrayType);
		rets.PushBack(true, allocator);
		onResult(ctx, document, source, "resp", ctx->handle(), ref, rets);
	}
	void dClose(ContextPtr ctx, const std::string& pattern, uint32_t source, const std::string& ref, Document& document, const Value& param)
	{
		assert(pattern == "cast");
		ctx->exit();
		LOG_DEBUG << "exit";
	}
	void dConnection(ContextPtr ctx, const std::string& pattern, uint32_t source, const std::string& ref, Document& document, const Value& param)
	{
		assert(source == master_);
		assert(pattern == "cast");
		assert(param.IsArray());
		assert(param.Size() == 1);
		assert(param[0].IsInt());
		int fd = param[0].GetInt();

		std::shared_ptr<Connection> conn(new Connection());
		conn->id = connectionR(conn);
		conn->fd = fd;
		
		fd2id_.insert({conn->fd, conn->id});
		ctx->channel(conn->fd)->enableReading()->update();
		//LOG_TRACE << "new connection fd=" << conn->fd << ",id=" << conn->id;
	}
	
	void dRead(ContextPtr ctx, const std::string& pattern, uint32_t source, const std::string& ref, Document& document, const Value& param)
	{
		//LOG_TRACE << "httpd dRead onEventId_.size()=" << onEventId_.size() << ",readCmd_.size()=" << readCmd_.size();
		assert(pattern == "call");
		if (onEventId_.size() == 0) {
			readCmd_.push(std::unique_ptr<ReadCmd>(new ReadCmd(pattern, source, ref)));
			//LOG_TRACE << "httpd dRead onEventId_.size() == 0";
			return;
		}
		uint32_t id = onEventId_.front();
		onEventId_.pop();
		auto conn = connectionG(id);
		if (!conn) {
			readCmd_.push(std::unique_ptr<ReadCmd>(new ReadCmd(pattern, source, ref)));
			//LOG_TRACE << "httpd dRead !conn";
			return;
		}
		
		HttpContext context;
		uint32_t nparsed = context.parse(conn->inputBuffer_.peek(), conn->inputBuffer_.peek() + conn->inputBuffer_.readableBytes());
		if (nparsed == 0) {
			readCmd_.push(std::unique_ptr<ReadCmd>(new ReadCmd(pattern, source, ref)));
			//LOG_TRACE << "httpd dRead nparsed == 0";
			return;
		}
		//LOG_TRACE << "httpd dRead parse ok id=" << conn->id << ",fd=" << conn->fd << ",nparsed=" << nparsed;
		assert(context.gotAll());
		conn->inputBuffer_.retrieve(nparsed);
		Value rets = httpPackageToJson(ctx, document, id, context.request());
		onResult(ctx, document, source, "resp", master_, ref, rets);

		conn->request.swap(context.request());
		infoCountR_++;
		
	}
	void dWrite(ContextPtr ctx, const std::string& pattern, uint32_t source, const std::string& ref, Document& document, const Value& param)
	{
		assert(pattern == "cast");
		assert(param.IsArray());
		assert(param.Size() == 2);
		assert(param[0].IsInt());
		assert(param[1].IsObject());
		uint32_t id = param[0].GetInt();
		const Value& response = param[1];
		auto conn = connectionG(id);
		if (!conn) {
			return;
		}

		HttpResponse resp;
		resp.setVersion(conn->request.version() == HttpRequest::kHttp11? "HTTP/1.1" : "HTTP/1.0");
		resp.setCode(404);
		if (response.HasMember("status") && response["status"].IsInt()) {
			resp.setCode(response["status"].GetInt());
		}
		if (response.HasMember("code") && response["code"].IsInt()) {
			resp.setCode(response["code"].GetInt());
		}
		if (response.HasMember("body") && response["body"].IsString()) {
			resp.setBody(response["body"].GetString());
		}
		if (response.HasMember("header") && response["header"].IsObject()) {
			const Value& headers = response["header"];
			for (auto& header : headers.GetObject()) {
				if (header.name.IsString() && header.value.IsString()) {
					resp.setHeader(header.name.GetString(), header.value.GetString());
				}
			}
		}
		const std::string str = resp.toString();
		//LOG_TRACE << "id=" << conn->id << ",fd=" << conn->fd << ",response=\r\n" << str;
		
		const void* data = static_cast<const void*>(str.data());
		size_t len = static_cast<size_t>(str.size());
		ssize_t nwrote = 0;
		size_t remaining = len;
		bool faultError = false;
		
		// if no thing in output queue, try writing directly
		if (!ctx->channel(conn->fd)->isWriting() && conn->outputBuffer_.readableBytes() == 0) {
			nwrote = sockets::write(conn->fd, data, len);
			if (nwrote >= 0) {
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
			conn->outputBuffer_.append(static_cast<const char*>(data)+nwrote, remaining);
			if (!ctx->channel(conn->fd)->isWriting()) {
				ctx->channel(conn->fd)->enableWriting()->update();
			}
		} else {
			const std::string connection = conn->request.header("Connection");
			bool close = connection == "close" || (conn->request.version() == HttpRequest::kHttp10 && connection != "Keep-Alive");
			conn->request.clear();
			if (close) {
				ctx->channel(conn->fd)->disableAll()->update();
				fd2id_.erase(conn->fd);
				connectionU(conn->id);
				sockets::shutdownWrite(conn->fd);
			}
			infoCountW_++;
		}
	}
	
	
	void ondRead(ContextPtr ctx, uint64_t eventtime, uint32_t eventtype, int eventfd)
	{
		assert(fd2id_.find(eventfd) != fd2id_.end());
		uint32_t id = fd2id_[eventfd];
		auto conn = connectionG(id);
		assert(conn->id == id);
		//LOG_TRACE << "httpd ondRead onEventId_.size()=" << onEventId_.size() << ",readCmd_.size()=" << readCmd_.size() << ",id=" << conn->id << ",fd=" << conn->fd;
		int savedErrno = 0;
		ssize_t n = conn->inputBuffer_.readFd(eventfd, &savedErrno);
		if (n == 0) {
			ctx->channel(conn->fd)->disableAll()->update();
			fd2id_.erase(conn->fd);
			connectionU(conn->id);
			sockets::close(conn->fd);
			//LOG_TRACE << "httpd ondRead close connection id=" << conn->id << ",fd=" << conn->fd;
			return;
		}
		while (conn->inputBuffer_.readableBytes() > 0) {
			if (readCmd_.size() == 0) {
				onEventId_.push(conn->id);
				//LOG_TRACE << "httpd ondRead readCmd_.size() == 0";
				return;
			}
			HttpContext context;
			uint32_t nparsed = context.parse(conn->inputBuffer_.peek(), conn->inputBuffer_.peek() + conn->inputBuffer_.readableBytes());
			if (nparsed == 0) {
				//LOG_TRACE << "httpd ondRead nparsed == 0";
				return;
			}
			LOG_TRACE << "httpd ondRead parse ok id=" << conn->id << ",fd=" << conn->fd << ",nparsed=" << nparsed;
			Document document;
			auto readCmd = std::move(readCmd_.front());
			readCmd_.pop();
			assert(context.gotAll());
			conn->inputBuffer_.retrieve(nparsed);
			Value rets = httpPackageToJson(ctx, document, id, context.request());
			onResult(ctx, document, readCmd->source, "resp", master_, readCmd->ref, rets);

			conn->request.swap(context.request());
			infoCountR_++;
		}
		LOG_TRACE << "onlRead end";
	}
	void ondWrite(ContextPtr ctx, uint64_t eventtime, uint32_t eventtype, int eventfd)
	{
		if (fd2id_.find(eventfd) == fd2id_.end()) {
			return;
		}
		uint32_t id = fd2id_[eventfd];
		auto conn = connectionG(id);
		assert(conn->id == id);

		if (ctx->channel(conn->fd)->isWriting()) {
			ssize_t n = sockets::write(conn->fd, conn->outputBuffer_.peek(), conn->outputBuffer_.readableBytes());
			if (n > 0) {
				conn->outputBuffer_.retrieve(n);
				if (conn->outputBuffer_.readableBytes() == 0) {
					ctx->channel(conn->fd)->disableWriting()->update();
					const std::string connection = conn->request.header("Connection");
					bool close = connection == "close" || (conn->request.version() == HttpRequest::kHttp10 && connection != "Keep-Alive");
					conn->request.clear();
					if (close) {
						ctx->channel(conn->fd)->disableAll()->update();
						fd2id_.erase(conn->fd);
						connectionU(conn->id);
						sockets::shutdownWrite(conn->fd);
					}
					infoCountW_++;
				}
			} else {
				char t_errnobuf[512] = {'\0'};
				strerror_r(errno, t_errnobuf, sizeof t_errnobuf);
				fprintf(stderr, "port handle = %d, fd = %d, errno = %d, write error = %s\n", ctx->handle(), conn->fd, errno, t_errnobuf);
				ctx->channel(conn->fd)->disableWriting()->update();
			}
		} else {
			//LOG_TRACE << "Connection fd = " << channel_->fd() << " is down, no more writing";
		}
	}
	void ondError(ContextPtr ctx, uint64_t eventtime, uint32_t eventtype, int eventfd)
	{
		assert(0);
	}
	void ondTimer(ContextPtr ctx, uint32_t type, uint32_t handle, uint32_t session, uint64_t ms)
	{
		assert(0);
	}



	void onResult(ContextPtr ctx, Document& document, uint32_t destination, const std::string& pattern, uint32_t source, const std::string& ref, Value& param)
	{
		Document::AllocatorType& allocator = document.GetAllocator();
		Value rets(kArrayType);
		rets.PushBack(Value(pattern.data(), pattern.size(), allocator).Move(), allocator);
		rets.PushBack(source,                               allocator);
		rets.PushBack(Value(ref.data(), ref.size(),         allocator).Move(), allocator);

		assert(param.IsArray());
		for (uint32_t i=0; i<param.Size(); i++) {
			rets.PushBack(param[i], allocator);
		}
		
		StringBuffer buffer;
		Writer<rapidjson::StringBuffer> writer(buffer);
		rets.Accept(writer);
		std::string rss = buffer.GetString();
		ctx->send(destination, ctx->makeMessage(MSG_TYPE_JSON, std::vector<char>(rss.begin(), rss.end())));
	}

	Value httpPackageToJson(ContextPtr ctx, Document& document, uint32_t id, const HttpRequest& request)
	{
		Document::AllocatorType& allocator = document.GetAllocator();
		Value rets(kArrayType);
		Value object(kObjectType);
		Value header(kObjectType);

		std::string method;
		if (request.method() == HttpRequest::kGet) {
			method = "GET";
		} else {
			assert(request.method() == HttpRequest::kPost);
			method = "POST";
		}

		object.AddMember(Value("method", allocator).Move(), Value(method.data(),  method.size(), allocator).Move(), allocator);
		object.AddMember(Value("path", allocator).Move(), Value(request.path().data(), request.path().size(), allocator).Move(), allocator);
		if (request.query().size() > 0) {
			object.AddMember(Value("query", allocator).Move(), Value(request.query().data(), request.query().size(), allocator).Move(), allocator);
		}
		if (request.body().size() > 0) {
			object.AddMember(Value("body", allocator).Move(), Value(request.body().data(), request.body().size(), allocator).Move(), allocator);
		}
		for (auto& m : request.header()) {
			header.AddMember(Value(m.first.data(), m.first.size(), allocator).Move(), Value(m.second.data(), m.second.size(), allocator).Move(), allocator);
		}
		object.AddMember(Value("header", allocator), header, allocator);
		rets.PushBack(ctx->handle(), allocator);
		rets.PushBack(id,            allocator);
		rets.PushBack(object,        allocator);

		return rets;
	}
	
private:
	enum class Type {kHttpL, kHttpD, kHttpC, kUnknown};
	Type httpType_;
	std::string infoStartT_;
	uint32_t    infoCountR_;
	uint32_t    infoCountW_;

	std::unordered_map<std::string, std::function<void(ContextPtr,std::string,uint32_t,std::string,rapidjson::Document&,const rapidjson::Value&)>> commands_;
	std::function<void(ContextPtr,uint64_t,uint32_t,int)>               onRead_;
	std::function<void(ContextPtr,uint64_t,uint32_t,int)>               onWrite_;
	std::function<void(ContextPtr,uint64_t,uint32_t,int)>               onError_;
	std::function<void(ContextPtr,uint32_t,uint32_t,uint32_t,uint64_t)> onTimer_;



	// listener
	uint32_t master_;
	int fd_;
	std::vector<uint32_t> workers_;
	int cur_;



	// httpd
	struct ReadCmd {
		ReadCmd(const std::string& p, uint32_t s, const std::string& r) : pattern(p), source(s), ref(r) { }
		std::string pattern;
		uint32_t source;
		std::string ref;
	};
	struct Connection {
		uint32_t id;
		int fd;
		HttpRequest request;
		Buffer inputBuffer_;
		Buffer outputBuffer_;
	};
	std::queue<std::unique_ptr<ReadCmd>> readCmd_;
	std::unordered_map<int,uint32_t> fd2id_;
	std::queue<uint32_t> onEventId_;

	uint32_t connsize_;
	uint32_t connindex_;
	std::vector<std::shared_ptr<Connection>> connections_;
	uint32_t connectionR(std::shared_ptr<Connection> conn)
	{
		#define CONN_MASK 0xffffffff
		while (true) {
			for(uint32_t i=0; i<connsize_; i++) {
				uint32_t id = (i + connindex_) & CONN_MASK;
				uint32_t hash = id & (connsize_ - 1);
				if (!connections_[hash] && id>0) {
					connections_[hash] = conn;
					connindex_ = id + 1;
					return id;
				}
			}
			assert((connsize_*2 - 1) < CONN_MASK);	
			std::vector<std::shared_ptr<Connection>> connections(connsize_*2, std::shared_ptr<Connection>());
			for (uint32_t i=0; i<connsize_; i++) {
				uint32_t hash = connections_[i]->id & (connsize_*2-1);
				assert(connections_[i].get() != NULL);
				connections[hash] = connections_[i];
			}

			connections_.swap(connections);
			connsize_ *= 2;
		}
		return 0;
	}
	bool connectionU(uint32_t id)
	{
		bool ret = false;
		uint32_t hash = id & (connsize_ - 1);
		std::shared_ptr<Connection> conn = connections_[id];
		if (conn && conn->id == id) {
			connections_[hash] = std::shared_ptr<Connection>();
			ret = true;
		}
		return ret;
	}
	std::shared_ptr<Connection> connectionG(uint32_t id)
	{
		std::shared_ptr<Connection> result;
		uint32_t hash = id & (connsize_ - 1);
		if (connections_[hash] && connections_[hash]->id == id) {
			result = connections_[hash];
		}
		return result;
	}

	Log log;
};


module(http)
