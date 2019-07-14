#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"  
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "Logger/Log.h"
#include "sockets.h"
#include "Actor.h"
#include "udns.h"
#include <assert.h>
#include <stdint.h>
#include <functional>
#include <string>
#include <set>
#include <map>


namespace {
int init_udns()
{
	static bool initialized = false;
	if (!initialized) {
		::dns_init(NULL, 0);
	}
	initialized = true;
	return 1;
}

struct UdnsInitializer {
	UdnsInitializer()
	{
		init_udns();
	}

	~UdnsInitializer()
	{
		::dns_reset(NULL);
	}
};
UdnsInitializer udnsInitializer;
const bool kDebug = false;
}

using namespace std::placeholders;
using namespace rapidjson;


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

		log.ctx    = ctx.get();
		log.handle = ctx->env().logger;

		dnsctx_         = NULL;
		dnsfd_          = -1;
		dnstimerActive_ = false;

		assert(message->type == MSG_TYPE_JSON);
		auto& data = message->data;
		Document document;
		document.Parse(static_cast<char*>(&*data.begin()), data.size());
		assert(document.IsArray());
		assert(document.Size() == 1);
		const Value& value = document[0];
		if (value.ObjectEmpty()) {
			init_udns();
			dnsctx_ = ::dns_new(NULL);
			assert(dnsctx_ != NULL);
			::dns_set_opt(dnsctx_, DNS_OPT_TIMEOUT, 2);
		} else {
			assert(value.HasMember("ip"));
			assert(value.HasMember("port"));
			assert(value.HasMember("ipv6"));
			assert(value["ip"].IsString());
			assert(value["port"].IsInt());
			assert(value["ipv6"].IsBool());

			std::string ip   = value["ip"].GetString();
			uint16_t    port = value["port"].GetInt();
			bool        ipv6 = value["ipv6"].GetBool();

			struct sockaddr_in6 addr6;
			struct sockaddr_in  addr4;
			struct sockaddr* addr = NULL;
			bzero(&addr6, sizeof(addr6));
			bzero(&addr4, sizeof(addr4));
			if (ipv6) {
				addr = sockets::fromIpPort(ip.c_str(), port, &addr6);
			} else {
				addr = sockets::fromIpPort(ip.c_str(), port, &addr4);
			}

			init_udns();
			dnsctx_ = ::dns_new(NULL);
			assert(dnsctx_ != NULL);
			::dns_add_serv_s(dnsctx_, addr);
			::dns_set_opt(dnsctx_, DNS_OPT_TIMEOUT, 2);
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

		ctx->channel(dnsfd_)->disableAll()->update();
		::dns_free(dnsctx_);
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
			
			assert(eventfd == dnsfd_);
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
		dnsfd_ = ::dns_open(dnsctx_);
		assert(dnsfd_ >= 0);
		ctx->channel(dnsfd_)->enableReading()->update();

		Document::AllocatorType& allocator = document.GetAllocator();
		Value refs;
		Value rets(kArrayType);
		refs.SetString(ref.data(), ref.size(), allocator);
		rets.PushBack("resp", allocator);
		rets.PushBack(ctx->handle(), allocator);
		rets.PushBack(refs, allocator);
		rets.PushBack(true, allocator);
		rets.PushBack(dnsfd_, allocator);
		
		StringBuffer buffer;
		Writer<rapidjson::StringBuffer> writer(buffer);
		rets.Accept(writer);
		std::string rss = buffer.GetString();
		ctx->send(source, ctx->makeMessage(MSG_TYPE_JSON, std::vector<char>(rss.begin(), rss.end())));

		LOG_DEBUG << "start";
	}
	void resolve(ContextPtr ctx, const std::string& pattern, uint32_t source, const std::string& ref, Document& document, const Value& param)
	{
		assert(pattern == "call");
		assert(param.Size() == 1);
		assert(param[0].IsString());
		std::string hostname = param[0].GetString();

		QueryData* queryData = new QueryData(this, ctx.get(), pattern, source, ref);
		assert(queryData != NULL);

		time_t now = time(NULL);
		struct dns_query* query = ::dns_submit_a4(dnsctx_, hostname.c_str(), 0, &dns::dns_query_a4, queryData);
		int timeout = ::dns_timeouts(dnsctx_, -1, now);
		uint32_t session = 0;
		if (!dnstimerActive_) {
			session = ctx->newsession();
			timeout = timeout<0 ? 2 : timeout;
			ctx->timeout(session, timeout * 1000);
			dnstimerActive_ = true;
		}
		assert(query != NULL);
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
		time_t now = time(NULL);
		::dns_ioevent(dnsctx_, now);
		LOG_DEBUG << "onEvent";
	}
	void onTimer(ContextPtr ctx, uint32_t type, uint32_t handle, uint32_t session, uint64_t ms)
	{
		assert(dnstimerActive_ == true);
		time_t now = time(NULL);
		int timeout = ::dns_timeouts(dnsctx_, -1, now);
		if (timeout < 0) {
			dnstimerActive_ = false;
		} else {
			uint32_t session = ctx->newsession();
			ctx->timeout(session, timeout * 1000);
		}
		LOG_DEBUG << "onTimer,timeout" << timeout;
	}
	void onQueryResult(struct dns_rr_a4 *result, Context* ctx, const std::string& pattern, uint32_t source, const std::string& ref)
	{
		Document document;
		Document::AllocatorType& allocator = document.GetAllocator();

		Value rets(kArrayType);
		Value refs;
		refs.SetString(ref.data(), ref.size(), allocator);
		rets.PushBack("resp", allocator);
		rets.PushBack(ctx->handle(), allocator);
		rets.PushBack(refs, allocator);

		int status = ::dns_status(dnsctx_);
		if (result) {
			if (kDebug) {
				printf("cname %s\n", result->dnsa4_cname);
				printf("qname %s\n", result->dnsa4_qname);
				printf("ttl %d\n", result->dnsa4_ttl);
				printf("nrr %d\n", result->dnsa4_nrr);
			}
			for (int i = 0; i < result->dnsa4_nrr; ++i) {
				char buf[64];
				::dns_ntop(AF_INET, &result->dnsa4_addr[i], buf, sizeof buf);
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

	static void dns_query_a4(struct dns_ctx *dnsctx, struct dns_rr_a4 *result, void *data)
	{
		
		QueryData* queryData = static_cast<QueryData*>(data);

		assert(dnsctx == queryData->owner->dnsctx_);
		queryData->owner->onQueryResult(result, queryData->ctx, queryData->pattern, queryData->source, queryData->ref);
		free(result);
		delete queryData;
	}
private:
	dns_ctx* dnsctx_;
	int      dnsfd_;
	bool     dnstimerActive_;
	std::map<std::string, std::function<void(ContextPtr,std::string,uint32_t,std::string,rapidjson::Document&,const rapidjson::Value&)>> commands_;
	Log log;
};

module(dns)