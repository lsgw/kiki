#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"  
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "Logger/Log.h"
#include "Actor.h"
#include <assert.h>
#include <stdint.h>
#include <functional>
#include <string>
#include <set>
#include <map>

using namespace std::placeholders;
using namespace rapidjson;


class Launcher : public Actor<Launcher> {
public:
	void init(ContextPtr ctx, MessagePtr& message) override
	{
		commands_.insert({"launch",   std::bind(&Launcher::serviceLaunch,   this, _1, _2, _3)});
		commands_.insert({"chmod",    std::bind(&Launcher::serviceChmod,    this, _1, _2, _3)});
		commands_.insert({"kill",     std::bind(&Launcher::serviceKill,     this, _1, _2, _3)});
		commands_.insert({"exit",     std::bind(&Launcher::serviceExit,     this, _1, _2, _3)});

		assert(message->type == MSG_TYPE_JSON);
		auto& data = message->data;
		Document document;
		document.Parse(static_cast<char*>(&*data.begin()), data.size());
		
		assert(document.IsArray());
		assert(document.Size() >= 3);

		launchLogger(ctx, document, document[0]);
		launchSignal(ctx, document, document[1]);
		for (SizeType i=2; i<document.Size(); i++) {
			rapidjson::Value& m = document[i];

			assert(m.IsObject());
			assert(m.HasMember("service"));
			assert(m.HasMember("args"));
			assert(m["service"].IsString());
			assert(m["args"].IsArray());
			
			std::string service    = m["service"].GetString();
			rapidjson::Value array = m["args"].GetArray();

			StringBuffer buffer;
			Writer<rapidjson::StringBuffer> writer(buffer);
			array.Accept(writer);
			std::string args = buffer.GetString();

			uint32_t handle = ctx->newcontext(service, 0, MSG_TYPE_JSON, std::vector<char>(args.begin(), args.end()));
			assert(handle > 0);

			std::string launchinfo = service + "," + args.substr(1, args.size()-2);
			service = service=="Snlua"? array[0].GetString() : service;

			assert(services_.find(handle) == services_.end());
			services_[handle].parameters = launchinfo;
			serviceHandleToModule_.insert({handle, service});
			serviceModuleToHandle_.insert({service, handle});
		}
		assert(services_.find(ctx->handle()) == services_.end());
		services_[ctx->handle()].parameters = "Launcher,...";
		ctx->env().launcher = ctx->handle();
		
		LOG_INFO << "Launch Launcher";
	}

	bool receive(ContextPtr ctx, MessagePtr& message) override
	{
		assert(message->type == MSG_TYPE_JSON);
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

		LOG_TRACE << "Launcher receive message from source(" << Fmt("%010d", source) << "):" << std::string(data.begin(), data.end());

		auto it = commands_.find(funcname);
		assert(it != commands_.end());
		auto func = it->second;

		Value param(kArrayType);     // function param

		for (uint32_t i=4; i<array.Size(); i++) {
			param.PushBack(array[i], allocator);
		}

		Value rets = func(ctx, document, param);
		if (pattern == "call") {
			Value returnarray(kArrayType);
			returnarray.PushBack("resp", allocator);
			returnarray.PushBack(ctx->handle(), allocator);
			returnarray.PushBack(array[2], allocator);
			for (uint32_t i=0; i<rets.Size(); i++) {
				returnarray.PushBack(rets[i], allocator);
			}
			StringBuffer buffer;
			Writer<rapidjson::StringBuffer> writer(buffer);
			returnarray.Accept(writer);
			std::string returnstring = buffer.GetString();

			ctx->send(source, ctx->makeMessage(MSG_TYPE_JSON, std::vector<char>(returnstring.begin(), returnstring.end())));
		}

		return true;
	}
	void launchLogger(ContextPtr ctx, Document& document, const Value& param)
	{
		assert(param.IsObject());
		assert(param.HasMember("service"));
		assert(param.HasMember("args"));
		assert(param["service"].IsString());
		assert(param["args"].IsObject());
		
		std::string service = param["service"].GetString();
		assert(service == "Logger");

		StringBuffer buffer;
		Writer<rapidjson::StringBuffer> writer(buffer);
		param["args"].Accept(writer);
		std::string args = buffer.GetString();

		uint32_t handle = ctx->newcontext(service, 0, MSG_TYPE_JSON, std::vector<char>(args.begin(), args.end()));
		assert(handle > 0);

		std::string launchinfo = service + "," + args;
		assert(services_.find(handle) == services_.end());
		services_[handle].parameters = launchinfo;
		serviceHandleToModule_.insert({handle, service});
		serviceModuleToHandle_.insert({service, handle});

		ctx->env().logger = handle;
		log.ctx    = ctx.get();
		log.handle = handle;
	}
	void launchSignal(ContextPtr ctx, Document& document, const Value& param)
	{
		assert(param.IsObject());
		assert(param.HasMember("service"));
		assert(param.HasMember("args"));
		assert(param["service"].IsString());
		assert(param["args"].IsObject());
		
		std::string service = param["service"].GetString();
		assert(service == "Signal");

		StringBuffer buffer;
		Writer<rapidjson::StringBuffer> writer(buffer);
		param["args"].Accept(writer);
		std::string args = buffer.GetString();

		uint32_t handle = ctx->newport(service, 0, MSG_TYPE_JSON, std::vector<char>(args.begin(), args.end()));
		assert(handle > 0);

		std::string launchinfo = service + "," + args;
		assert(services_.find(handle) == services_.end());
		services_[handle].parameters = launchinfo;
		serviceHandleToModule_.insert({handle, service});
		serviceModuleToHandle_.insert({service, handle});

		ctx->env().signal = handle;
	}
	Value serviceLaunch(ContextPtr ctx, Document& document, const Value& param)
	{
		Document::AllocatorType& allocator = document.GetAllocator();
		Value rets(kArrayType);
		assert(param.Size() >= 3);
		assert(param[0].IsString());
		assert(param[1].IsInt());
		assert(param[2].IsString());
		
		std::string type    = param[0].GetString();
		uint32_t    owner   = param[1].GetInt();
		std::string service = param[2].GetString();
		assert(services_.find(owner) != services_.end());
		
		Value serviceparam(kArrayType);
		for (uint32_t i=3; i<param.Size(); i++) {
			Value value;
			value.CopyFrom(param[i], allocator);
			serviceparam.PushBack(value, allocator);
		}

		StringBuffer buffer;
		Writer<rapidjson::StringBuffer> writer(buffer);
		serviceparam.Accept(writer);
		std::string args = buffer.GetString();

		uint32_t handle = 0;
		if (type == "service") {
			handle = ctx->newcontext(service, 0, MSG_TYPE_JSON, std::vector<char>(args.begin(), args.end()));
			assert(handle > 0);
			assert(services_.find(handle) == services_.end());
		} else {
			assert(type == "port");
			handle = ctx->newport(service, owner, MSG_TYPE_JSON, std::vector<char>(args.begin(), args.end()));
			assert(handle > 0);
			assert(services_.find(handle) == services_.end());
			services_[owner].followers.insert(handle);
			services_[handle].followersd.insert(owner);
		}
		
		std::string launchinfo = service + "," + args.substr(1, args.size()-2);
		services_[handle].parameters = launchinfo;

		rets.PushBack(handle, allocator);
		LOG_DEBUG << "launch handle(" << Fmt("%010d", handle) << ") " << launchinfo;

		return rets;
	}
	Value serviceChmod(ContextPtr ctx, Document& document, const Value& param)
	{
		Document::AllocatorType& allocator = document.GetAllocator();
		Value rets(kArrayType);
		assert(param.Size() == 3);
		assert(param[0].IsInt());
		assert(param[1].IsInt());
		assert(param[2].IsInt());
		
		uint32_t destport = param[0].GetInt();
		uint32_t oldowner = param[1].GetInt();
		uint32_t newowner = param[1].GetInt();
		if (services_.find(destport) == services_.end()) {
			return rets;
		}
		if (services_.find(oldowner) != services_.end()) {
			assert(services_[oldowner].followers.find(destport) != services_[oldowner].followers.end());
			services_[oldowner].followers.erase(destport);
			services_[destport].followersd.erase(oldowner);
		}
		if (services_.find(newowner) != services_.end()) {
			assert(services_[newowner].followers.find(destport) == services_[newowner].followers.end());
			services_[newowner].followers.insert(destport);
			services_[destport].followersd.insert(newowner);
		} else {
			ctx->send(destport, ctx->makeMessage(MSG_TYPE_EXIT));
		}
		
		rets.PushBack(newowner, allocator);
		return rets;
	}
	Value serviceKill(ContextPtr ctx, Document& document, const Value& param)
	{
		// Document::AllocatorType& allocator = document.GetAllocator();
		Value rets(kArrayType);
		assert(param.Size() == 1);
		assert(param[0].IsInt());
		uint32_t handle = param[0].GetInt();

		if (services_.find(handle) == services_.end()) {
			LOG_DEBUG << "kill handle(" << Fmt("%010d", handle) << ") fail [reason: no find service]";
		} else {
			ctx->send(handle, ctx->makeMessage(MSG_TYPE_EXIT));
		}
		return rets;
	}

	Value serviceExit(ContextPtr ctx, Document& document, const Value& param)
	{
		Document::AllocatorType& allocator = document.GetAllocator();
		Value rets(kArrayType);
		assert(param.Size() == 1);
		assert(param[0].IsInt());
		uint32_t handle = param[0].GetInt();
		assert(services_.find(handle) != services_.end());

		LOG_DEBUG << "handle(" << Fmt("%010d", handle) << ") exit";
		auto ith = serviceHandleToModule_.find(handle);
		if (ith != serviceHandleToModule_.end()) {
			std::string service = ith->second;
			assert(serviceModuleToHandle_.find(service) != serviceModuleToHandle_.end());
			serviceModuleToHandle_.erase(service);
			serviceHandleToModule_.erase(handle);
		}


		auto& followersd = services_[handle].followersd;
		for (uint32_t d : followersd) {
			assert(services_.find(d) != services_.end());
			services_[d].followers.erase(handle);
		}
		auto& followers = services_[handle].followers;
		for (uint32_t f : followers) {
			assert(services_.find(f) != services_.end());
			services_[f].followersd.erase(handle);
			ctx->send(f, ctx->makeMessage(MSG_TYPE_EXIT));
		}
		


		auto& monitorsd = services_[handle].monitorsd;
		for (uint32_t d : monitorsd) {
			assert(services_.find(d) != services_.end());
			services_[d].monitors.erase(handle);

			Value returnarray(kArrayType);
			returnarray.PushBack("exit", allocator);
			returnarray.PushBack(ctx->handle(), allocator);
			returnarray.PushBack("ref", allocator);
			returnarray.PushBack(handle, allocator);
			returnarray.PushBack("normal", allocator);

			StringBuffer buffer;
			Writer<rapidjson::StringBuffer> writer(buffer);
			returnarray.Accept(writer);
			std::string returnstring = buffer.GetString();

			ctx->send(d, ctx->makeMessage(MSG_TYPE_JSON, std::vector<char>(returnstring.begin(), returnstring.end())));
		}

		auto& monitors = services_[handle].monitors;
		for (uint32_t m : monitors) {
			assert(services_.find(m) != services_.end());
			services_[m].monitorsd.erase(handle);
		}
		


		auto& links = services_[handle].links;
		for (uint32_t u : links) {
			assert(services_.find(u) != services_.end());
			services_[u].links.erase(handle);
			ctx->send(u, ctx->makeMessage(MSG_TYPE_EXIT));
		}

		
		services_.erase(handle);
		
		return rets;
	}

private:
	struct ServiceInfo {
		std::string parameters;
		std::set<uint32_t> followers;
		std::set<uint32_t> followersd;
		std::set<uint32_t> monitors;
		std::set<uint32_t> monitorsd;
		std::set<uint32_t> links;
	};
	std::map<uint32_t,ServiceInfo> services_;
	std::map<std::string, uint32_t> serviceModuleToHandle_;
	std::map<uint32_t, std::string> serviceHandleToModule_;
	std::map<std::string, std::function<rapidjson::Value(ContextPtr,rapidjson::Document&,const rapidjson::Value&)>> commands_;
	Log log;
};

module(Launcher)