#include "Timer.h"
#include <sys/time.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <sys/select.h>

#define TIVR_BITS		16
#define TIVN_BITS		12
#define TIVR_SIZE		(1 << TIVR_BITS)  // 65536
#define TIVN_SIZE		(1 << TIVN_BITS)  // 4096
#define TIVR_MASK       (TIVR_SIZE - 1)
#define TIVN_MASK       (TIVN_SIZE - 1)

uint64_t gettimetous()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	uint64_t cp = (uint64_t)tv.tv_sec * 1000000 + (uint64_t)tv.tv_usec;
	return cp;
}

Timer::Timer() :
	looping_(),
	quit_(),
	spin(),
	interval(1000),
	current(gettimetous()/interval),
	jiffies(0)
{
	tv[0].resize(TIVR_SIZE);
	tv[1].resize(TIVN_SIZE);
	tv[2].resize(TIVN_SIZE);
	tv[3].resize(TIVN_SIZE);
	tv[4].resize(TIVN_SIZE);

	for (int l=0; l<5; l++) {
		for (auto i=0; i<tv[l].size(); i++) {
			tv[l][i].head.next = NULL;
			tv[l][i].tail = &(tv[l][i].head);
		}
	}
}
Timer::~Timer()
{
	if (!quit_) {
		quit();
	}
	spin.lock();
	for (int l=0; l<5; l++) {
		for (auto i=0; i<tv[l].size(); i++) {
			Node* head = tv[l][i].head.next;
			tv[l][i].head.next = NULL;
			tv[l][i].tail = &(tv[l][i].head);
			while (head) {
				Node* node = head->next;
				delete head;
				head = node;
			}
		}
	}
	spin.unlock();
}
void Timer::loop()
{
	struct timeval val;
	val.tv_sec = 0;
	val.tv_usec = 250;
	assert(!looping_);
	looping_ = true;
	while (!quit_) {
		update();
		// int res = select(0, NULL, NULL, NULL, &val);
		// assert(res <= 0);
		usleep(250);
	}
	looping_ = false;
}
void Timer::quit()
{
	quit_ = true;
}

void Timer::timeout(uint32_t type, uint32_t context, uint32_t session, uint64_t time)
{
	if (time == 0) {
		callback(type, context, session, time);
	} else {
		Node* node = new Node;
		node->next    = NULL;
		node->type    = type;
		node->context = context;
		node->session = session;
		node->time    = time;
		spin.lock();
		node->expire  = jiffies + time;
		add(node);
		spin.unlock();
	}
}
void Timer::update()
{
	uint64_t coming = gettimetous() / interval;
	if (coming < current) {
		fprintf(stderr, "time diff error: change from %lld to %lld\n", coming, current);
		current = coming;
	} else if (current != coming) {
		uint64_t diff = coming - current;
		// printf("current=%lld, coming=%lld, diff=%lld\n", current, coming, diff);
		for (uint64_t i=0; i<diff; i++) {
			shift();
			execute();
		}
		current = coming;
	}
}

void Timer::add(Node* node)
{
	uint64_t last   = 0;
	uint64_t offset = TIVR_BITS;
	uint64_t mask   = ((uint64_t)1 << offset) - 1;
	uint64_t expire = node->expire;
	
	int l = 0;
	int i = 0;
	for (; l<4; l++) {
		if ((expire|mask) == (jiffies|mask)) {
			i = (expire & mask) >> last;
			break;
		}
		last = offset;
		offset += TIVN_BITS;
		mask = ((uint64_t)1 << offset) - 1;
	}
	//printf("Timer::add type:%u, context:%u, session:%u, time:%lld --> l:%d,i:%d\n", node->type, node->context, node->session, node->time, l, i);
	tv[l][i].tail->next = node;
	tv[l][i].tail = node;
}

void Timer::shift()
{
	spin.lock();
	jiffies++;
	uint64_t last   = 0;
	uint64_t offset = TIVR_BITS;
	uint64_t mask   = ((uint64_t)1 << offset) - 1;
	int l = 1;
	while ((jiffies & mask) == 0) {
		last = offset;
		offset += TIVN_BITS;
		mask = ((uint64_t)1 << offset) - 1;
		int i = (jiffies & mask) >> last;
		if (i != 0) {
			Node* head = tv[l][i].head.next;
			tv[l][i].head.next = NULL;
			tv[l][i].tail = &(tv[l][i].head);
			while (head) {
				Node* node = head->next;
				head->next = NULL;
				add(head);
				head = node;
			}

			break;
		}
		l++;
		assert(l > 0 && l < 5);
	}
	spin.unlock();
}

void Timer::execute()
{
	uint32_t offset = TIVR_BITS;
	uint32_t mask = ((uint64_t)1 << offset) - 1;
	spin.lock();
	int i = jiffies & mask;
	while (tv[0][i].head.next) {
		Node* head = tv[0][i].head.next;
		tv[0][i].head.next = NULL;
		tv[0][i].tail = &(tv[0][i].head);
		spin.unlock();
		while (head) {
			Node* node = head->next;
			// printf("Timer::execute type:%u, context:%u, session:%u, time:%lld --> jiffies:%lld, expire:%lld\n", head->type, head->context, head->session, head->time, jiffies, head->expire);
			callback(head->type, head->context, head->session, head->time);
			delete head;
			head = node;
		}
		spin.lock();
	}
	spin.unlock();
}
