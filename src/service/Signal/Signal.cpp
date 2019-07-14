#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"  
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "Logger/Log.h"
#include "sockets.h"
#include "Actor.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <functional>
#include <string>
#include <set>
#include <map>
#include <list>


using namespace std::placeholders;
using namespace rapidjson;

static int  sig_fd[2] = {0, 0};
static void sig_handler(int id)
{
	ssize_t n = sockets::write(sig_fd[1], &id, sizeof(id));
	if (n != sizeof(id)) {
		fprintf(stderr, "sig handler writes %zd bytes\n", n);
	} else {
    	//fprintf(stdout, "收到了信号 %s\n", strsignal(id));
	}
}



class Siganl : public Actor<Siganl> {
public:
	void init(ContextPtr ctx, MessagePtr& message) override
	{
		std::list<std::shared_ptr<Handler>> sigmap;

		sigmap.push_back(std::dynamic_pointer_cast<Handler>(std::shared_ptr<SIGNORMHandler>(new SIGNORMHandler("SIGHUP",    SIGHUP))));    // 终止进程       终端线路挂断
		sigmap.push_back(std::dynamic_pointer_cast<Handler>(std::shared_ptr<SIGNORMHandler>(new SIGNORMHandler("SIGINT",    SIGINT))));    // 终止进程       来自键盘的中断 ctrl-z
		sigmap.push_back(std::dynamic_pointer_cast<Handler>(std::shared_ptr<SIGNORMHandler>(new SIGNORMHandler("SIGQUIT",   SIGQUIT))));   // 终止进程       来自键盘的退出 ctrl-c
		sigmap.push_back(std::dynamic_pointer_cast<Handler>(std::shared_ptr<SIGNORMHandler>(new SIGNORMHandler("SIGILL",    SIGILL))));    // 终止进程       非法指令
		sigmap.push_back(std::dynamic_pointer_cast<Handler>(std::shared_ptr<SIGNORMHandler>(new SIGNORMHandler("SIGTRAP",   SIGTRAP))));   // 建立CORE文件   跟踪自陷
		sigmap.push_back(std::dynamic_pointer_cast<Handler>(std::shared_ptr<SIGNORMHandler>(new SIGNORMHandler("SIGABRT",   SIGABRT))));   // 建立CORE文件   来自 abort 函数的终止信号
		sigmap.push_back(std::dynamic_pointer_cast<Handler>(std::shared_ptr<SIGNORMHandler>(new SIGNORMHandler("SIGBUS",    SIGBUS))));    // 终止进程       总线错误
		sigmap.push_back(std::dynamic_pointer_cast<Handler>(std::shared_ptr<SIGNORMHandler>(new SIGNORMHandler("SIGFPE",    SIGFPE))));    // 建立CORE文件   浮点异常
		sigmap.push_back(std::dynamic_pointer_cast<Handler>(std::shared_ptr<SIGNORMHandler>(new SIGNORMHandler("SIGKILL",   SIGKILL))));   // 终止进程       杀死进程
		sigmap.push_back(std::dynamic_pointer_cast<Handler>(std::shared_ptr<SIGNORMHandler>(new SIGNORMHandler("SIGUSR1",   SIGUSR1))));   // 终止进程       用户定义信号1
		sigmap.push_back(std::dynamic_pointer_cast<Handler>(std::shared_ptr<SIGNORMHandler>(new SIGNORMHandler("SIGSEGV",   SIGSEGV))));   // 建立CORE文件   段非法错误
		sigmap.push_back(std::dynamic_pointer_cast<Handler>(std::shared_ptr<SIGNORMHandler>(new SIGNORMHandler("SIGUSR2",   SIGUSR2))));   // 终止进程       用户定义信号2
		sigmap.push_back(std::dynamic_pointer_cast<Handler>(std::shared_ptr<SIGNORMHandler>(new SIGNORMHandler("SIGPIPE",   SIGPIPE))));   // 终止进程       向一个没有读用户的管道写数据
		sigmap.push_back(std::dynamic_pointer_cast<Handler>(std::shared_ptr<SIGNORMHandler>(new SIGNORMHandler("SIGALRM",   SIGALRM))));   // 终止进程       来自 alarm 函数的定时器信号
		sigmap.push_back(std::dynamic_pointer_cast<Handler>(std::shared_ptr<SIGNORMHandler>(new SIGNORMHandler("SIGTERM",   SIGTERM))));   // 终止进程       软件终止信号
		sigmap.push_back(std::dynamic_pointer_cast<Handler>(std::shared_ptr<SIGCHLDHandler>(new SIGCHLDHandler("SIGCHLD",   SIGCHLD))));   // 忽略信号       当子进程停止或退出时通知父进程的信号
		sigmap.push_back(std::dynamic_pointer_cast<Handler>(std::shared_ptr<SIGNORMHandler>(new SIGNORMHandler("SIGCONT",   SIGCONT))));   // 忽略信号       继续执行一个停止的进程
		sigmap.push_back(std::dynamic_pointer_cast<Handler>(std::shared_ptr<SIGNORMHandler>(new SIGNORMHandler("SIGSTOP",   SIGSTOP))));   // 停止进程       非终端来的停止信号
		sigmap.push_back(std::dynamic_pointer_cast<Handler>(std::shared_ptr<SIGNORMHandler>(new SIGNORMHandler("SIGTSTP",   SIGTSTP))));   // 停止进程       终端来的停止信号
		sigmap.push_back(std::dynamic_pointer_cast<Handler>(std::shared_ptr<SIGNORMHandler>(new SIGNORMHandler("SIGTTIN",   SIGTTIN))));   // 停止进程       后台进程从终端读
		sigmap.push_back(std::dynamic_pointer_cast<Handler>(std::shared_ptr<SIGNORMHandler>(new SIGNORMHandler("SIGTTOU",   SIGTTOU))));   // 停止进程       后台进程向终端写
		sigmap.push_back(std::dynamic_pointer_cast<Handler>(std::shared_ptr<SIGNORMHandler>(new SIGNORMHandler("SIGURG",    SIGURG))));    // 忽略信号       套接字上的紧急情况信号
		sigmap.push_back(std::dynamic_pointer_cast<Handler>(std::shared_ptr<SIGNORMHandler>(new SIGNORMHandler("SIGXCPU",   SIGXCPU))));   // 终止进程       CPU时限超时
		sigmap.push_back(std::dynamic_pointer_cast<Handler>(std::shared_ptr<SIGNORMHandler>(new SIGNORMHandler("SIGXFSZ",   SIGXFSZ))));   // 终止进程       文件长度过长
		sigmap.push_back(std::dynamic_pointer_cast<Handler>(std::shared_ptr<SIGNORMHandler>(new SIGNORMHandler("SIGVTALRM", SIGVTALRM)))); // 终止进程       虚拟计时器到时
		sigmap.push_back(std::dynamic_pointer_cast<Handler>(std::shared_ptr<SIGNORMHandler>(new SIGNORMHandler("SIGPROF",   SIGPROF))));   // 终止进程       统计分布图用计时器到时
		sigmap.push_back(std::dynamic_pointer_cast<Handler>(std::shared_ptr<SIGNORMHandler>(new SIGNORMHandler("SIGWINCH",  SIGWINCH))));  // 忽略信号       窗口大小发生变化
		sigmap.push_back(std::dynamic_pointer_cast<Handler>(std::shared_ptr<SIGNORMHandler>(new SIGNORMHandler("SIGIO",     SIGIO))));     // 终止进程       描述符上可以进行I/O操作
		sigmap.push_back(std::dynamic_pointer_cast<Handler>(std::shared_ptr<SIGNORMHandler>(new SIGNORMHandler("SIGSYS",    SIGSYS))));    // 终止进程       无效的系统调用
	#ifdef __MACH__
		sigmap.push_back(std::dynamic_pointer_cast<Handler>(std::shared_ptr<SIGNORMHandler>(new SIGNORMHandler("SIGEMT",    SIGEMT))));
		sigmap.push_back(std::dynamic_pointer_cast<Handler>(std::shared_ptr<SIGNORMHandler>(new SIGNORMHandler("SIGINFO",   SIGINFO))));
	#else
		sigmap.push_back(std::dynamic_pointer_cast<Handler>(std::shared_ptr<SIGNORMHandler>(new SIGNORMHandler("SIGPWR",    SIGPWR))));    // 终止进程       电源故障
		sigmap.push_back(std::dynamic_pointer_cast<Handler>(std::shared_ptr<SIGNORMHandler>(new SIGNORMHandler("SIGSTKFLT", SIGSTKFLT)))); // 终止进程       协处理器上的栈故障
	#endif

		for (auto& sig : sigmap) {
			shandler_.insert({sig->name,  sig});
		}
		for (auto& sig : sigmap) {
			ihandler_.insert({sig->id,    sig});
		}

		commands_.insert({"unsubscribe", std::bind(&Siganl::unsubscribe, this, _1, _2, _3, _4, _5, _6)});
		commands_.insert({"subscribe",   std::bind(&Siganl::subscribe,   this, _1, _2, _3, _4, _5, _6)});
		commands_.insert({"close",       std::bind(&Siganl::close,       this, _1, _2, _3, _4, _5, _6)});

		// int ret = socketpair(AF_UNIX, SOCK_DGRAM,  0, sig_fd);
		int ret = socketpair(AF_UNIX, SOCK_STREAM, 0, sig_fd);
		assert(ret == 0);
		ctx->channel(sig_fd[0])->enableReading()->update();

		log.ctx    = ctx.get();
		log.handle = ctx->env().logger;

		assert(message->type == MSG_TYPE_JSON);
		auto& data = message->data;
		Document document;
		document.Parse(static_cast<char*>(&*data.begin()), data.size());
		assert(document.IsObject());

		for (auto& sig : sigmap) {
			std::string signame = sig->name;
			if (document.HasMember(signame.c_str())) {
				assert(document[signame.c_str()].IsString());
				std::string mode = document[signame.c_str()].GetString();
				if (mode == "SIG_DFL") {
					sig->actionf(SIG_DFL);
				} else if (mode == "SIG_IGN") {
					sig->actionf(SIG_IGN);
				} else {
					sig->actionf(sig_handler);
				}
				LOG_DEBUG << "Action " << signame << ":" << mode;
			}
		}
		LOG_INFO << "Launch Signal";
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
			
			assert(eventfd == sig_fd[0]);
			onEvent(ctx, eventtime, eventtype, eventfd);
		} else {
			assert(0);
		}
		return true;
	}
	void release(ContextPtr ctx, MessagePtr& message) override
	{
		Document document;
		Document::AllocatorType& allocator = document.GetAllocator();
		Value array(kArrayType);
		array.PushBack("cast", allocator);
		array.PushBack(ctx->handle(), allocator);
		array.PushBack("ref",  allocator);
		array.PushBack("exit", allocator);
		array.PushBack(ctx->handle(), allocator);

		StringBuffer buffer;
		Writer<rapidjson::StringBuffer> writer(buffer);
		array.Accept(writer);
		std::string args = buffer.GetString();
		ctx->send(ctx->env().launcher, ctx->makeMessage(MSG_TYPE_JSON, std::vector<char>(args.begin(), args.end())));

		ctx->channel(sig_fd[0])->disableAll()->update();
		::close(sig_fd[0]);
		::close(sig_fd[1]);
	}
	void unsubscribe(ContextPtr ctx, const std::string& pattern, uint32_t source, const std::string& ref, Document& document, const Value& param)
	{
		assert(pattern == "cast");
		assert(param.IsArray());
		assert(param.Size() >= 2);
		assert(param[0].IsInt());
		assert(param[1].IsString());
		uint32_t handle = param[0].GetInt();
		std::string signame = param[1].GetString();
		assert(shandler_.find(signame) != shandler_.end());
		bool ok = shandler_[signame]->unsubscribe(document, param);
		assert(ok);
		LOG_DEBUG << "Cancel " << signame << " service(" << Fmt("%010d", handle) << ")";
	}
	void subscribe(ContextPtr ctx, const std::string& pattern, uint32_t source, const std::string& ref, Document& document, const Value& param)
	{
		assert(pattern == "cast");
		assert(param.IsArray());
		assert(param.Size() >= 2);
		assert(param[0].IsInt());
		assert(param[1].IsString());
		uint32_t handle = param[0].GetInt();
		std::string signame = param[1].GetString();
		assert(shandler_.find(signame) != shandler_.end());
		bool ok = shandler_[signame]->subscribe(document, param);
		assert(ok);
		LOG_DEBUG << "Action " << signame << " service(" << Fmt("%010d", handle) << ")"; 
	}
	void close(ContextPtr ctx, const std::string& pattern, uint32_t source, const std::string& ref, Document& document, const Value& param)
	{
		assert(pattern == "cast");
		for (auto& m : shandler_) {
			m.second->actionf(SIG_DFL);
		}
		ctx->exit();
		LOG_INFO << "Exit Siganl";
	}
	void onEvent(ContextPtr ctx, uint64_t eventtime, uint32_t eventtype, int eventfd)
	{
		int sig = 0;
		int n = sockets::read(eventfd, &sig, sizeof(sig));
		assert(n == sizeof(sig));
		assert(ihandler_.find(sig) != ihandler_.end());
		std::vector<uint32_t> services = ihandler_[sig]->onEvent();

		Document document;
		Document::AllocatorType& allocator = document.GetAllocator();
		Value signame;
		signame.SetString(ihandler_[sig]->name.data(), ihandler_[sig]->name.size(), allocator);
		Value array(kArrayType);
		array.PushBack("cast",        allocator);
		array.PushBack(ctx->handle(), allocator);
		array.PushBack("ref",         allocator);
		array.PushBack("signal",      allocator);
		array.PushBack(signame,       allocator);

		StringBuffer buffer;
		Writer<rapidjson::StringBuffer> writer(buffer);
		array.Accept(writer);
		std::string args = buffer.GetString();
		for (uint32_t handle : services) {
			ctx->send(handle, ctx->makeMessage(MSG_TYPE_JSON, std::vector<char>(args.begin(), args.end())));
		}
		LOG_DEBUG << "onEvent sig " << sig;
	}
	
private:
	struct Handler {
		Handler(const std::string& s, int i) : name(s), id(i), force(false) { }
		virtual ~Handler() { }
		void actionf(void(*handler)(int))
		{
			force = false;
			action(handler);
			force = true;
		}
		void action(void(*handler)(int))
		{
			if (force) {
				return;
			}
			struct sigaction sa;
			sigfillset(&sa.sa_mask);
			sa.sa_flags = 0;
			sa.sa_handler = handler;
			int ret = sigaction(id, &sa, NULL);
			assert(ret == 0);
		}
		virtual std::vector<uint32_t> onEvent() = 0;
		virtual bool unsubscribe(Document& document, const Value& param) = 0;
		virtual bool subscribe(Document& document, const Value& param) = 0;
		const std::string name;
		const int id;
		bool force;
	};
	struct SIGNORMHandler : public Handler {
	public:
		SIGNORMHandler(const std::string& s, int i) : Handler(s, i) { }
		std::vector<uint32_t> onEvent() override
		{
			return std::vector<uint32_t>(services.begin(), services.end());
		}
		bool unsubscribe(Document& document, const Value& param) override
		{
			assert(param.IsArray());
			assert(param.Size() == 2);
			assert(param[0].IsInt());
			assert(param[1].IsString());
			uint32_t handle = param[0].GetInt();
			std::string signame = param[1].GetString();
			assert(signame == name);
			assert(services.find(handle) != services.end());
			services.erase(handle);
			if (services.size() == 0) {
				action(SIG_DFL);
			}
			return true;
		}
		bool subscribe(Document& document, const Value& param) override
		{
			assert(param.IsArray());
			assert(param.Size() == 2);
			assert(param[0].IsInt());
			assert(param[1].IsString());
			uint32_t handle = param[0].GetInt();
			std::string signame = param[1].GetString();
			assert(signame == name);
			if (services.size() == 0) {
				action(sig_handler);
			}
			services.insert(handle);
			return true;
		}
	private:
		std::set<uint32_t> services;
	};
	struct SIGCHLDHandler : public Handler{
	public:
		SIGCHLDHandler(const std::string& s, int i) : Handler(s, i) { }
		std::vector<uint32_t> onEvent() override
		{
			pid_t pid;
			int status;
			int savedErrno = 0;
			std::vector<uint32_t> v;
			savedErrno = errno;         /* In case we modify 'errno' */
			while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
				for (auto& m : services) {
					if (m.second.find(pid) != m.second.end()) {
						v.push_back(m.first);
					}
				}
			}
			errno = savedErrno;
			return v;
		}
		bool unsubscribe(Document& document, const Value& param) override
		{
			assert(param.IsArray());
			assert(param.Size() == 3);
			assert(param[0].IsInt());
			assert(param[1].IsString());
			assert(param[2].IsInt());
			uint32_t handle = param[0].GetInt();
			std::string signame = param[1].GetString();
			pid_t pid = param[2].GetInt();
			assert(signame == name);
			assert(services.find(handle) != services.end());
			assert(services[handle].find(pid) != services[handle].end());
			services[handle].erase(pid);
			if (services[handle].size() == 0) {
				services.erase(handle);
			}
			if (services.size()) {
				action(SIG_DFL);
			}
			return true;
		}
		bool subscribe(Document& document, const Value& param) override
		{
			assert(param.IsArray());
			assert(param.Size() == 3);
			assert(param[0].IsInt());
			assert(param[1].IsString());
			assert(param[2].IsInt());
			uint32_t handle = param[0].GetInt();
			std::string signame = param[1].GetString();
			pid_t pid = param[2].GetInt();
			assert(signame == name);
			if (services.size() == 0) {
				action(sig_handler);
			}
			services[handle].insert(pid);
			return true;
		}
	private:
		std::map<uint32_t, std::set<pid_t>> services;
	};

	std::map<std::string, std::shared_ptr<Handler>> shandler_;
	std::map<int,         std::shared_ptr<Handler>> ihandler_;
	std::map<std::string, std::function<void(ContextPtr,std::string,uint32_t,std::string,rapidjson::Document&,const rapidjson::Value&)>> commands_;
	Log log;
};

module(Siganl)