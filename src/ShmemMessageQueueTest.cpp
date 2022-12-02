#define DEFINE_STATICS
#include "../include/ShmemMessageQueue.h"
#include <thread>

const size_t TIMES=10000;

namespace {
  __attribute__((hot, always_inline))
    inline int64_t epochNanos() {
      __sync_synchronize();
      timespec tp;
      clock_gettime(CLOCK_REALTIME, &tp);
      int64_t ret = int64_t(tp.tv_sec)*1000*1000*1000 + int64_t(tp.tv_nsec);
      __sync_synchronize();
      return ret;
    }
  #define testFATAL(...) do { \
    fprintf(stderr, ##__VA_ARGS__); \
    exit(-1); \
  } while (0)
}


int64_t maxLatency = -1, minLatency = -1;

template <typename T>
void run1(T* queue1, T* queue2) {
  int64_t readcount = 0;
  for (size_t i = 0 ; i < TIMES; ++i) {
    usleep(10);
    int64_t t = epochNanos();
    {
      auto w = queue1->nextWriteSlot();
      *w = t;
    }
    while (1) {
      auto r = queue2->recv(readcount);
      if (r) { break; }
    }
  }
}

int64_t total = 0, totCount = 0;
template <typename T>
void run2(T* queue1, T* queue2) {
  int64_t readcount = 0;
  for (size_t i = 0 ; i < TIMES; ++i) {
    while (1) {
      auto r = queue1->recv(readcount);
      if (r) {
        int64_t diff = epochNanos() - *r;
        total += diff;
        totCount += 1;
        if(maxLatency == -1 || diff > maxLatency) maxLatency = diff;
        if(minLatency == -1 || diff < minLatency) minLatency = diff;
        break;
      }
    }
    {
      int64_t t = epochNanos();
      auto w = queue2->nextWriteSlot();
      *w = t;
    }
  }
}

int main(int, char**) {
  if (unlink("/dev/shm/my.testMemoryQueueLatency.1.txt") == -1 && errno != ENOENT) testFATAL("unlink: %s", strerror(errno));
  if (unlink("/dev/shm/my.testMemoryQueueLatency.2.txt") == -1 && errno != ENOENT) testFATAL("unlink: %s", strerror(errno));
  auto* myQueue1 = Salvo::ShmemMessageQueue::create<int64_t>("my.testMemoryQueueLatency.1.txt", true);
  auto* myQueue2 = Salvo::ShmemMessageQueue::create<int64_t>("my.testMemoryQueueLatency.2.txt", true);

  std::thread t( [=]() { run1(myQueue1, myQueue2); } );
  run2(myQueue1, myQueue2);
  t.join();
  printf("Latency: %.4f nanos (max=%zd, min=%zd)\n", total / double(totCount?totCount:1), maxLatency, minLatency);
  if (unlink("/dev/shm/my.testMemoryQueueLatency.1.txt") == -1) testFATAL("unlink: %s", strerror(errno));
  if (unlink("/dev/shm/my.testMemoryQueueLatency.2.txt") == -1) testFATAL("unlink: %s", strerror(errno));
}
