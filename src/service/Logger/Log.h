#ifndef LOG_H
#define LOG_H

#include "Context.h"
#include "Message.h"
#include <assert.h>
#include <string.h>
#include <time.h>
#include <string>
#include <vector>
#include <algorithm>

std::string switchLogLevelToString(uint32_t level)
{
	if (level == 0) {
		return "[TRACE]";
	} else if (level == 1) {
		return "[DEBUG]";
	} else if (level == 2) {
		return "[INFO ]";
	} else if (level == 3) {
		return "[WARN ]";
	} else if (level == 4) {
		return "[ERROR]";
	} else if (level == 5) {
		return "[FATAL]";
	} else {
		return "what happen";
	}
}

const int kMaxNumericSize = 32;
const char digits[] = "9876543210123456789";
const char* zero = digits + 9;
static_assert(sizeof(digits)==20, "error");


class Fmt {
public:
	template<typename... T>
	Fmt(const char* fmt, T&&... val)
	{
		size_ = snprintf(buf_, sizeof(buf_), fmt, std::forward<T>(val)...);
	}
	Fmt(const char* fmt)
	{
		size_ = snprintf(buf_, sizeof(buf_), "%s", fmt);
	}

	const char* data() const { return buf_; }
	int size() const { return size_; }
	
private:
	char buf_[32];
	int size_;
};

template<typename T>
size_t convert(char buf[], T value)
{
	T i = value;
	char* p = buf;

	do {
		int lsd = static_cast<int>(i % 10);
		i /= 10;
		*p++ = zero[lsd];
	} while (i != 0);

	if (value < 0) {
		*p++ = '-';
	}
	*p = '\0';
	std::reverse(buf, p);

	return p - buf;
}

const char* getFilename(const char *p)
{
    char ch = '/';
    const char* q = strrchr(p, ch);
 	
    return q? q + 1 : p;
}


class LogStream {
public:
	LogStream(Context* ctx, uint32_t handle, int level, const std::string& filename, int fileline, uint32_t locals) :
		buffer_(4096),
		cur_(0),
		ctx_(ctx),
		handle_(handle),
		locals_(locals)
	{
		uint32_t source = ctx->handle();
		char* p = &*buffer_.begin();
		::memcpy(p, &source, sizeof(source));
		p += sizeof(source);
		::memcpy(p, &level, sizeof(level));
		p += sizeof(level);
		cur_ += sizeof(source) + sizeof(level);

		time_t now = 0;
		char timebuf[32];
		struct tm tm;
		now = time(NULL);
		localtime_r(&now, &tm);
		// gmtime_r(&now, &tm);
		strftime(timebuf, sizeof timebuf, "%Y-%m-%d %H:%M:%S", &tm);
		*this << "[";
		*this << timebuf;
		*this << " ";
		*this << getFilename(filename.c_str());
		*this << " ";
		*this << fileline;
		*this << "] ";
	}
	~LogStream()
	{
		if (locals_ == 1) {
			char* p = &*buffer_.begin();
			uint32_t handle = 0;
			uint32_t level  = 0;
			::memcpy(&handle, p, sizeof(handle));
			p += sizeof(handle);
			::memcpy(&level, p, sizeof(level));
			p += sizeof(level);

			std::string msg = switchLogLevelToString(level) + " ";
			msg.append(p, buffer_.size() - sizeof(handle) - sizeof(level));
			fprintf(stdout, "[%010d] %.*s\n", handle, static_cast<int>(msg.size()), msg.data());
		} else {
			ctx_->send(handle_, ctx_->makeMessage(MSG_TYPE_LOG, std::move(buffer_)));
		}
	}
	

	LogStream& operator<<(bool v)
	{
		append(v? "1":"0", 1);
		return *this;
	}

	LogStream& operator<<(short v)
	{
		formatInteger(v);
		return *this;
	}
	LogStream& operator<<(unsigned short v)
	{
		formatInteger(v);
		return *this;
	}

	LogStream& operator<<(int v)
	{
		formatInteger(v);
		return *this;
	}
	LogStream& operator<<(unsigned int v)
	{
		formatInteger(v);
		return *this;
	}

	LogStream& operator<<(long v)
	{
		formatInteger(v);
		return *this;
	}
	LogStream& operator<<(unsigned long v)
	{
		formatInteger(v);
		return *this;
	}

	LogStream& operator<<(long long v)
	{
		formatInteger(v);
		return *this;
	}
	LogStream& operator<<(unsigned long long v)
	{
		formatInteger(v);
		return *this;
	}



	LogStream& operator<<(char v)
	{
		append(&v, 1);
		return *this;
	}
	LogStream& operator<<(float v)
	{
		*this << static_cast<double>(v);
		return *this;
	}
	LogStream& operator<<(double v)
	{
		ensureWritableBytes(kMaxNumericSize);
		int len = snprintf(&*buffer_.begin()+cur_, kMaxNumericSize, "%.12g", v);
		assert(len <= avail());
		cur_ += len;
		return *this;
	}




	LogStream& operator<<(const std::string& v)
	{
		append(v.c_str(), v.size());
		return *this;
	}
	LogStream& operator<<(const char* str)
	{
		if (str) {
			append(str, strlen(str));
		} else {
			append("(null)", 6);
		}
		return *this;
	}
	LogStream& operator<<(const unsigned char* str)
	{
		return operator<<(reinterpret_cast<const char*>(str));
	}
	LogStream& operator<<(const Fmt& fmt)
	{
		append(fmt.data(), fmt.size());
		return *this;
	}



	std::string toString() const
	{
		return std::string(buffer_.begin(), buffer_.begin()+cur_);
	}

	void append(const char* data, size_t len)
	{
		ensureWritableBytes(len);
		std::copy(data, data+len, &*buffer_.begin()+cur_);
		assert(len <= avail());
		cur_ += len;
	}
private:
	template<typename T>
	void formatInteger(T v)
	{
		ensureWritableBytes(kMaxNumericSize);
		size_t len = convert(&*buffer_.begin()+cur_, v);
		assert(len <= avail());
		cur_ += len;
	}

	size_t avail() const
	{
		return buffer_.size() - cur_;
	}
	void ensureWritableBytes(size_t len)
	{
		if (avail() < len) {
			buffer_.resize(avail()+len);
		}
		assert(avail() >= len);
	}
	
	std::vector<char> buffer_;
	int cur_;

	Context* ctx_;
	uint32_t handle_;
	uint32_t locals_;
};

class Log {
public:
	const static int TRACE = 0;  //指出比DEBUG粒度更细的一些信息事件 (开发过程使用)
	const static int DEBUG = 1;  //指出细粒度信息事件对调试应用程序是非常有帮助（开发过程使用)
	const static int INFO  = 2;  //表明消息在粗粒度级别上突出强调应用程序的运行过程
	const static int WARN  = 3;  //系统能正常运行，但可能会出现潜在错误的情形
	const static int ERROR = 4;  //指出虽然发生错误事件，但仍然不影响系统的继续运行
	const static int FATAL = 5;  //指出每个严重的错误事件将会导致应用程序的退出
	
	Log() : 
		ctx(NULL), 
		handle(0),
		locals(0)
	{

	}
	LogStream stream(int level, const std::string& filename, int fileline)
	{
		return LogStream(ctx, handle, level, filename, fileline, locals);
	}
	Context* ctx;
	uint32_t handle;
	uint32_t locals;
};

#define LOG_TRACE log.stream(Log::TRACE, __FILE__, __LINE__)
#define LOG_DEBUG log.stream(Log::DEBUG, __FILE__, __LINE__)
#define LOG_INFO  log.stream(Log::INFO,  __FILE__, __LINE__)
#define LOG_WARN  log.stream(Log::WARN,  __FILE__, __LINE__)
#define LOG_ERROR log.stream(Log::ERROR, __FILE__, __LINE__)
#define LOG_FATAL log.stream(Log::FATAL, __FILE__, __LINE__)


#endif