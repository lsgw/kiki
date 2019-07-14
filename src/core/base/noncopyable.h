#ifndef NONCOPYABLE_H
#define NONCOPYABLE_H

class noncopyable {
public:
	noncopyable() = default;
	noncopyable(const noncopyable&) = delete;
	noncopyable& operator=(const noncopyable&) = delete;
};

#endif
