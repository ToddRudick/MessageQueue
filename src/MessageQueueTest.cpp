/**
  * Copyright (C) 2020 Salvo Limited Hong Kong
  * 
  *  Licensed under the Apache License, Version 2.0 (the "License");
  *  you may not use this file except in compliance with the License.
  *  You may obtain a copy of the License at
  *
  *      http://www.apache.org/licenses/LICENSE-2.0
  *
  *  Unless required by applicable law or agreed to in writing, software
  *  distributed under the License is distributed on an "AS IS" BASIS,
  *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  *  See the License for the specific language governing permissions and
  *  limitations under the License.
  *
***/
#include "../include/MessageQueue.hpp"
#include <thread>
#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MAIN
#include <boost/test/included/unit_test.hpp>
BOOST_AUTO_TEST_CASE( MessageQueueTest )
{
  using namespace Salvo;
  struct NODE {
    char data[256];
    int i;
  };
  MessageQueue<NODE> mq;
  static_assert(std::is_standard_layout<MessageQueue<NODE> >::value,
  "Message Queue must have C style layout (for layout confirmation)");
  static_assert( ((void*)mq._header._typeCheck) == ((void*)&mq), "MessageQueue header must be at the start");
  printf("Type of object: %s\n", mq._header._typeCheck);
  mq.confirmHeader();
  int64_t readcount = 0;
  auto msg = mq.recv(readcount);
  BOOST_REQUIRE(msg == NULL);
  BOOST_REQUIRE(msg == nullptr);
  for (bool useLockedWriter: std::vector<bool>{false, true, false, true}) { 
    int64_t startcount = readcount;
    if (useLockedWriter) {
      fprintf(stderr, "Testing with locking writer\n");
    } else {
      fprintf(stderr, "Testing with non-locking writer\n");
    }
    int trackingCount = 0;
    for (int i=0; i <= 50; ++i) {
      {
        if (useLockedWriter) {
          auto wrt = mq.nextWriteSlotLocked();
          wrt->i = i;
          snprintf(wrt->data, sizeof(wrt->data), "This is #%d", i);
          if (i % 10 != 0) {
            ++trackingCount;
          } else {
            wrt.abandon();
          }

        } else {
          auto wrt = mq.nextWriteSlot();
          wrt->i = i;
          snprintf(wrt->data, sizeof(wrt->data), "This is #%d", i);
          if (i % 10 != 0) {
            ++trackingCount;
          } else {
            wrt.abandon();
          }
        }
      }
      if ((i % 5) == 0) {
        for (int j=0; j<10;++j) {
          auto msg = mq.recv(readcount);
          if (trackingCount) {
            BOOST_REQUIRE(msg);
            int expected = readcount - startcount + 1;
            if (expected>=10) expected += 1;
            if (expected>=20) expected += 1;
            if (expected>=30) expected += 1;
            if (expected>=40) expected += 1;
            if (expected>=50) expected += 1;
            char buf[1024];
            snprintf(buf,sizeof(buf),"This is #%d", expected);
            BOOST_REQUIRE(std::string(buf) == std::string(msg->data));
            --trackingCount;
          }
        }
      }
    }
    // catch up
    while (mq.recv(readcount)) { }
  }
  // Racing threads test

  {
    struct ThreadTestNode {
      int64_t instance;
      int64_t instanceZeroTimerVal;
      int64_t myOwnTimerVal;
    };
    MessageQueue<ThreadTestNode> mq2;
    MessageQueue<ThreadTestNode> mq3;
    std::vector<std::thread> workers;
    #define NUM_WORKERS 7
    #define RUN_COUNT 6400LL
    std::vector<int64_t> workerCounts(NUM_WORKERS);
    for (int64_t i = 0; i < NUM_WORKERS; ++i) {
      workers.push_back(std::thread([i,&mq2,&mq3,&workerCounts]() {
        if (i == 0) { // the leader & the reader
          int64_t totalTimeDiff = 0;
          int64_t totalTimeDiffCount = 0;
          int64_t readCount = 0;
          for (int64_t j = 0; j < RUN_COUNT; ++j) {
            {
              auto f = mq2.nextWriteSlot();
              { // sleep-spin
                auto startSpin = f.MQNanos();
                while (startSpin + 5000 > f.MQNanos() ) {
                }
              }
              
              f->instance = i;
              f->instanceZeroTimerVal = f.MQNanos();
              f->myOwnTimerVal = f->instanceZeroTimerVal;
            }
            do {
              auto msg = mq3.recv(readCount);
              if (msg) {
                workerCounts[msg->instance] += 1;
                if (msg->instance != i) {
                  totalTimeDiff += msg->myOwnTimerVal - msg->instanceZeroTimerVal;
                  totalTimeDiffCount += 1;
                }
              } else {
                usleep(1);
              }
            } while (readCount < (NUM_WORKERS - 1) * (j+1));
          }
          { // signal exit
            auto f = mq2.nextWriteSlotLocked();
            f->instance = 0;
            f->instanceZeroTimerVal = 0;
            f->myOwnTimerVal = 0;
          }
          fprintf(stderr, "Average time to receive message: %ld nanos\n", totalTimeDiff / std::max<int64_t>(1,totalTimeDiffCount));
        } else {
          int64_t readCount = 0;
          do {
            auto msg = mq2.recv(readCount);
            if (msg) {
              if (msg->instance == 0) {
                if (msg->instanceZeroTimerVal == 0) { break; }
                else {
                  auto f = mq3.nextWriteSlotLocked();
                  f->instance = i;
                  f->instanceZeroTimerVal = msg->instanceZeroTimerVal;
                  f->myOwnTimerVal = f.MQNanos();
                }
              }
            }
          } while (1);
        }
      }));
    }
    std::for_each(workers.begin(), workers.end(), [](std::thread &t) {
        t.join();
        });
    for (size_t i = 1; i < NUM_WORKERS; ++i) {
      int64_t count = workerCounts[i];
      if (count != RUN_COUNT) fprintf(stderr, "workerCounts[%ld] == %ld (!= %lld)\n", i, count, RUN_COUNT);
      BOOST_REQUIRE(count == RUN_COUNT);
    }
  }
}
