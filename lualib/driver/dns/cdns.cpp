#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"  
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "Logger/Log.h"
#include "sockets.h"
#include "ares.h"
#include "Actor.h"
#include <assert.h>
#include <stdint.h>
#include <netdb.h>
#include <arpa/inet.h>  // inet_ntop
#include <netinet/in.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <functional>
#include <string>
#include <set>
#include <map>



using namespace std::placeholders;
using namespace rapidjson;

namespace {
double getSeconds(struct timeval* tv)
{
	if (tv) {
		return double(tv->tv_sec) + double(tv->tv_usec)/1000000.0;
	} else {
		return -1.0;
	}
}

const char* getSocketType(int type)
{
	if (type == SOCK_DGRAM) {
		return "UDP";
	} else if (type == SOCK_STREAM) {
		return "TCP";
	} else {
		return "Unknown";
	}
}
const bool kDebug = false;
}

class dns : public Actor<dns> {
	struct QueryData {
		dns* owner;
		Context* ctx;
		std::string pattern;
		uint32_t source;
		std::string ref;
		QueryData(dns* o, Context* c, const std::string& p, uint32_t s, const std::string& r) :
			owner(o),
			ctx(c),
			pattern(p),
			source(s),
			ref(r)
		{

		}
	};
public:
	void init(ContextPtr ctx, MessagePtr& message) override
	{
		commands_.insert({"start",   std::bind(&dns::start,   this, _1, _2, _3, _4, _5, _6)});
		commands_.insert({"resolve", std::bind(&dns::resolve, this, _1, _2, _3, _4, _5, _6)});
		commands_.insert({"close",   std::bind(&dns::close,   this, _1, _2, _3, _4, _5, _6)});

		ctx_       = ctx.get();
		log.ctx    = ctx.get();
		log.handle = ctx->env().logger;

		assert(message->type == MSG_TYPE_JSON);
		auto& data = message->data;
		Document document;
		document.Parse(static_cast<char*>(&*data.begin()), data.size());
		assert(document.IsArray());
		assert(document.Size() == 1);
		const Value& value = document[0];
	
		bool hostfile = false;
		if (value.HasMember("hostfile")) {
			hostfile = value["hostfile"].GetBool();
		}

		static char lookups[] = "b";
		struct ares_options options;
		int optmask = ARES_OPT_FLAGS;
		options.flags = ARES_FLAG_NOCHECKRESP;
		options.flags |= ARES_FLAG_STAYOPEN;
		options.flags |= ARES_FLAG_IGNTC; // UDP only
		optmask |= ARES_OPT_SOCK_STATE_CB;
		options.sock_state_cb = &dns::ares_sock_state_callback;
		options.sock_state_cb_data = this;
		optmask |= ARES_OPT_TIMEOUT;
		options.timeout = 2;
		if (hostfile) {
			optmask |= ARES_OPT_LOOKUPS;
			options.lookups = lookups;
		}

		int status = ares_init_options(&dnsctx_, &options, optmask);
		assert(status == ARES_SUCCESS);
		ares_set_socket_callback(dnsctx_, &dns::ares_sock_create_callback, this);

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

		ares_destroy(dnsctx_);
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

			// log.trace("Launcher receive message from source(" + Fmt("%010d", source).string() + ") : " + std::string(data.begin(), data.end()));

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
			
			onEvent(ctx, eventtime, eventtype, eventfd);
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
		Document::AllocatorType& allocator = document.GetAllocator();
		Value refs;
		Value rets(kArrayType);
		refs.SetString(ref.data(), ref.size(), allocator);
		rets.PushBack("resp", allocator);
		rets.PushBack(ctx->handle(), allocator);
		rets.PushBack(refs, allocator);
		rets.PushBack(true, allocator);
		
		StringBuffer buffer;
		Writer<rapidjson::StringBuffer> writer(buffer);
		rets.Accept(writer);
		std::string rss = buffer.GetString();
		ctx->send(source, ctx->makeMessage(MSG_TYPE_JSON, std::vector<char>(rss.begin(), rss.end())));
	}
	void resolve(ContextPtr ctx, const std::string& pattern, uint32_t source, const std::string& ref, Document& document, const Value& param)
	{
		assert(pattern == "call");
		assert(param.Size() == 1);
		assert(param[0].IsString());
		std::string hostname = param[0].GetString();

		QueryData* queryData = new QueryData(this, ctx.get(), pattern, source, ref);
		assert(queryData != NULL);

		ares_gethostbyname(dnsctx_, hostname.c_str(), AF_INET, &dns::ares_host_callback, queryData);
		struct timeval tv;
		struct timeval* tvp = ares_timeout(dnsctx_, NULL, &tv);
		double timeout = getSeconds(tvp);
		uint32_t session = 0;
		if (!dnstimerActive_) {
			session = ctx->newsession();
			timeout = timeout<0 ? 2 : timeout;
			ctx->timeout(session, timeout * 1000);
			dnstimerActive_ = true;
		}
		LOG_DEBUG << "hostname=" << hostname << ",timeout=" << timeout << ",session=" << session << ",active=" << dnstimerActive_;
	}
	void close(ContextPtr ctx, const std::string& pattern, uint32_t source, const std::string& ref, Document& document, const Value& param)
	{
		assert(pattern == "cast");
		ctx->exit();
		LOG_DEBUG << "exit";
	}

	void onEvent(ContextPtr ctx, uint64_t eventtime, uint32_t eventtype, int eventfd)
	{
		 ares_process_fd(dnsctx_, eventfd, ARES_SOCKET_BAD);
		 LOG_DEBUG << "onEvent";
	}
	void onTimer(ContextPtr ctx, uint32_t type, uint32_t handle, uint32_t session, uint64_t ms)
	{
		assert(dnstimerActive_ == true);
		ares_process_fd(dnsctx_, ARES_SOCKET_BAD, ARES_SOCKET_BAD);
		struct timeval tv;
		struct timeval* tvp = ares_timeout(dnsctx_, NULL, &tv);
		double timeout = getSeconds(tvp);
		if (timeout < 0) {
			dnstimerActive_ = false;
		} else {
			uint32_t session = ctx->newsession();
			ctx->timeout(session, timeout * 1000);
		}
		LOG_DEBUG << "onTimer,timeout=" << timeout << ",session=" << session << ",active=" << dnstimerActive_;
	}

	
	void onQueryResult(int status, struct hostent* result, Context* ctx, const std::string& pattern, uint32_t source, const std::string& ref)
	{
		LOG_DEBUG << "onQueryResult";
		Document document;
		Document::AllocatorType& allocator = document.GetAllocator();

		Value rets(kArrayType);
		Value refs;
		refs.SetString(ref.data(), ref.size(), allocator);
		rets.PushBack("resp", allocator);
		rets.PushBack(ctx->handle(), allocator);
		rets.PushBack(refs, allocator);

		if (result) {
			if (kDebug) {
				printf("h_name %s\n", result->h_name);
				for (char** alias = result->h_aliases; *alias != NULL; ++alias) {
					printf("alias: %s\n", *alias);
				}
				// printf("ttl %d\n", ttl);
				// printf("h_length %d\n", result->h_length);
			}
			for (char** haddr = result->h_addr_list; *haddr != NULL; ++haddr) {
				char buf[64];
				inet_ntop(AF_INET, *haddr, buf, sizeof buf);
				Value ip;
				ip.SetString(buf, strlen(buf), allocator);
				rets.PushBack(ip, allocator);
				if (kDebug) {
					printf("  %s\n", buf);
				}
			}
		}
		StringBuffer buffer;
		Writer<rapidjson::StringBuffer> writer(buffer);
		rets.Accept(writer);
		std::string rss = buffer.GetString();
		ctx->send(source, ctx->makeMessage(MSG_TYPE_JSON, std::vector<char>(rss.begin(), rss.end())));
		if (kDebug) {
			printf("onQueryResult status=%d\n", status);
		}
	}

	void onSockCreate(int sockfd, int type)
	{
		LOG_TRACE << "onSockCreate sockfd=" << sockfd << ",type=" << getSocketType(type);
		ctx_->channel(sockfd)->enableReading()->update();
	}
	void onSockStateChange(int sockfd, bool read, bool write)
	{
		LOG_TRACE << "onSockStateChange sockfd=" << sockfd << ",read=" << read << ",write=" << write;
		if (read) {
			ctx_->channel(sockfd)->enableReading()->update();
		} else {
			ctx_->channel(sockfd)->disableReading()->update();
		}
		if (write) {
			ctx_->channel(sockfd)->enableWriting()->update();
		} else {
			ctx_->channel(sockfd)->disableWriting()->update();
		}
	}


	static void ares_host_callback(void* data, int status, int timeouts, struct hostent* hostent)
	{
		QueryData* queryData = static_cast<QueryData*>(data);
		queryData->owner->onQueryResult(status, hostent, queryData->ctx, queryData->pattern, queryData->source, queryData->ref);
		delete queryData;
	}
	static int ares_sock_create_callback(int sockfd, int type, void* data)
	{
		static_cast<dns*>(data)->onSockCreate(sockfd, type);
		return 0;
	}
	static void ares_sock_state_callback(void* data, int sockfd, int read, int write)
	{
		static_cast<dns*>(data)->onSockStateChange(sockfd, read, write);
	}


private:
	Context* ctx_;
	ares_channel dnsctx_;
	bool dnstimerActive_;
	std::map<std::string, std::function<void(ContextPtr,std::string,uint32_t,std::string,rapidjson::Document&,const rapidjson::Value&)>> commands_;
	Log log;
};

module(dns)