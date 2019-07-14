#ifndef MODULE_H
#define MODULE_H

#include "noncopyable.h"
#include "Message.h"
#include "RWLock.h"
#include <string>
#include <memory>
#include <vector>
#include <map>

typedef void*(*module_create)(void);
typedef bool(*module_callback)(ContextPtr ctx, void* actor, MessagePtr message);
typedef void(*module_release)(ContextPtr ctx, void* actor, MessagePtr message);

class Module : public noncopyable, public std::enable_shared_from_this<Module> {
public:
	Module(const std::string& name, void* dl, module_create create, module_callback callback, module_release release);
	~Module();
	
	void* create() { return create_(); }
	bool  callback(ContextPtr ctx, void* actor, MessagePtr message) { return callback_(ctx, actor, message); }
	void  release(ContextPtr ctx, void* actor, MessagePtr message) { release_(ctx, actor, message); }
	
	void* dl() const { return dl_; }
	const std::string name() const { return name_; }
private:
	const std::string name_;
	void* dl_;
	module_create   create_;
	module_callback callback_;
	module_release  release_;
};

class ModuleList : public noncopyable {
public:
	void setModulePath(const std::vector<std::string>& pathname);
	bool reload(const std::string& name);
	std::shared_ptr<Module> query(const std::string& name);
private:
	RWLock lock_;
	std::vector<std::string> paths_;
	std::map<std::string,std::weak_ptr<Module>> modules_;
};



#endif