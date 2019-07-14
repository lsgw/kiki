#include <unordered_map>
#include <string>
struct Proto {
public:
	Proto() : nextid_(0) { }

	uint32_t get(const std::string& protoname)
	{
		if (name2id_.find(protoname) != name2id_.end()) {
			return name2id_[protoname];
		} else {
			return 0;
		}
	}
	uint32_t add(const std::string& protoname)
	{
		uint32_t newid = 0;
		if (name2id_.find(protoname) == name2id_.end()) {
			newid = ++nextid_;
			name2id_[protoname] = newid;
			id2name_[newid] = protoname;
		}
		return newid;
	}
private:
	std::unordered_map<uint32_t, std::string> id2name_;
	std::unordered_map<std::string, uint32_t> name2id_;
	uint32_t nextid_;
};