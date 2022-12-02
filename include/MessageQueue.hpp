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
#ifndef __MessageQueue__
#define __MessageQueue__
#include <type_traits>
#include <time.h>
#include <unistd.h>
#include <limits>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <cstddef>
#include <cxxabi.h>
#include <stdexcept>
#include <string.h>

class MessageQueueTest;
namespace Salvo {

#if __cplusplus > 199711L
#define _MQCONSTEXPR constexpr
#else
#define _MQCONSTEXPR const
#endif

  struct MessageQueueTraits
  {
    _MQCONSTEXPR static size_t defaultSize = 1024 * 4;
  };

  template <class PAYLOAD, size_t SIZE_ELEMENTS = MessageQueueTraits::defaultSize>
    class MessageQueue {
#if __cplusplus > 199711L && __GNUG__ && __GNUC__ >= 5
      static_assert(std::is_trivially_copyable<PAYLOAD>::value, "PAYLOAD must be memcpyable");
#endif
      static_assert((SIZE_ELEMENTS & (SIZE_ELEMENTS-1)) == 0, "SIZE_ELEMENTS must be a power of two");
      public:
      explicit MessageQueue(const std::string &overrideName_ = "");
      typedef MessageQueue type;
      typedef PAYLOAD value_type;
      static _MQCONSTEXPR size_t capacity() { return SIZE_ELEMENTS; }
      private: struct MessageQueueWriteHandle; struct MessageQueueReadHandle; struct LockedMessageQueueWriteHandle;
               // reading and writing is done via smartpointers / handles that act as auto_ptr (ownership transferred on copy).
               // the writehandle will commit the record on going out of scope, unless abandon() is called
               // lockedwritehandle is the same, but works on a copy, which is placed in the "next element" in queue
               //   on going out of scope in such a way that it supports multiple writers
      public:
               struct LockedMessageQueueWriteHandle nextWriteSlotLocked();
               struct MessageQueueWriteHandle nextWriteSlot();
               // the read handle will increment readcount on going out of scope if there was data, unless abandon() is called.
               // it will return a value compatible with nullptr/NULL/false if there is nothing to read.
               struct MessageQueueReadHandle recv(volatile int64_t& readcount) const; 
               volatile int64_t writeCount() const { return(_header._onElement); }

               // compatibility methods
               void push_back(const PAYLOAD& val) { auto f = nextWriteSlot(); (*f) = val; }
               void push_back_locked(const PAYLOAD& val) { auto f = nextWriteSlotLocked(); (*f) = val; }

               // confirmHeader
               void confirmHeader(const std::string &overrideName_ = "") const;

               static _MQCONSTEXPR size_t headerSize() { return offsetof(type, _queue); }
      private:
               struct NODE {
                 PAYLOAD _data;
                 uint32_t _lapCount;
                 NODE(): _lapCount(0) { }
               };
               static _MQCONSTEXPR size_t mask() { return SIZE_ELEMENTS - 1; }
               struct MessageQueueHeader {
                 char _typeCheck[1024];
                 size_t _lengthCheck;
                 volatile int64_t _onElement;
                 MessageQueueHeader(): _lengthCheck(0), _onElement(0) { }
               } _header;
               NODE _queue[SIZE_ELEMENTS];

               struct LockedMessageQueueWriteHandle {
                 LockedMessageQueueWriteHandle(MessageQueue* mq): _mq(mq) { }
                 PAYLOAD _dataCopy;
                 PAYLOAD& operator* () { return _dataCopy; }
                 PAYLOAD* operator-> () { return &_dataCopy; }
                 void abandon() { _mq = NULL; }
                 int64_t MQNanos() {
                   timespec tp;
                   clock_gettime(CLOCK_REALTIME, &tp);
                   return int64_t(tp.tv_sec)*1000*1000*1000 + int64_t(tp.tv_nsec);
                 }
                 ~LockedMessageQueueWriteHandle() {
                   if (_mq != NULL) {
                     // use a special value of the lapcount to represent a lock-value
                     // the readers are all waiting for the value to increment by 1 so this reuse is fine
                     int64_t startSpinTime = 0;
                     uint64_t onElement;
                     do {
                       onElement = _mq->_header._onElement;
                       uint32_t sentinalValue = (onElement / type::capacity() + std::numeric_limits<uint32_t>::max()/2);
                       uint32_t *location = & _mq->_queue[onElement & type::mask()]._lapCount;
                       uint32_t oldValue = onElement / type::capacity();
                       if (__sync_bool_compare_and_swap(location, oldValue, sentinalValue)) {
                         break;
                       } else {
                         if (startSpinTime==0) {
                           startSpinTime = MQNanos();
                         } else {
                           int64_t now = MQNanos();
                           if (now - startSpinTime > 1000LL * 1000 * 1000) { // spun more than 1 second!
                             fprintf(stderr, "!!WARNING!! Spinlock error! Spun more than 1 second (trying to write %d to %ld (count %ld, old val %d)). Breaking lock!\n", 
                                 sentinalValue, onElement & type::mask(), onElement, oldValue);
                             break;
                           }
                         }
                       }
                     } while (1);
                     _mq->_queue[onElement & type::mask()]._data = _dataCopy;
                     _mq->_queue[onElement & type::mask()]._lapCount = 1 + _mq->_header._onElement / type::capacity();
                     ++_mq->_header._onElement;
                   }
                 }
                 LockedMessageQueueWriteHandle(const LockedMessageQueueWriteHandle& other): _mq(other._mq) {
                   (const_cast<LockedMessageQueueWriteHandle&>(other))._mq = NULL;
                 }
                 LockedMessageQueueWriteHandle& operator=(const LockedMessageQueueWriteHandle& other) {
                   if (this != &other) {
                     _mq = other._mq;
                     other._mq = NULL;
                   }
                   return *this;
                 }
                 private: type* _mq;
               };

               struct MessageQueueWriteHandle {
                 MessageQueueWriteHandle(MessageQueue* mq): _mq(mq) { }
                 int64_t MQNanos() {
                   timespec tp;
                   clock_gettime(CLOCK_REALTIME, &tp);
                   return int64_t(tp.tv_sec)*1000*1000*1000 + int64_t(tp.tv_nsec);
                 }
                 PAYLOAD& operator* () { return _mq->_queue[_mq->_header._onElement & type::mask()]._data; }
                 PAYLOAD* operator-> () { return &(_mq->_queue[_mq->_header._onElement & type::mask()]._data); }
                 void abandon() { _mq = NULL; }
                 ~MessageQueueWriteHandle() {
                   if (_mq != NULL) {
                     _mq->_queue[_mq->_header._onElement & type::mask()]._lapCount = 1 + _mq->_header._onElement / type::capacity();
                     ++_mq->_header._onElement;
                   }
                 }
                 MessageQueueWriteHandle(const MessageQueueWriteHandle& other): _mq(other._mq) {
                   (const_cast<MessageQueueWriteHandle&>(other))._mq = NULL;
                 }
                 MessageQueueWriteHandle& operator=(const MessageQueueWriteHandle& other) {
                   if (this != &other) {
                     _mq = other._mq;
                     other._mq = NULL;
                   }
                   return *this;
                 }
                 private: type* _mq;
               };
               struct MessageQueueReadHandle {
                 MessageQueueReadHandle(const MessageQueue* mq, volatile int64_t& readcount): _mq(mq), _ready(false), _readcount(&readcount) {
                   _ready = (mq->_queue[*_readcount & type::mask()]._lapCount == 1 + readcount / type::capacity()); 
                 }
                 const PAYLOAD& operator* () { return _ready ? (_mq->_queue[*_readcount & type::mask()]._data) : *((PAYLOAD*)NULL); }
                 const PAYLOAD* operator-> () { return _ready ? &(_mq->_queue[*_readcount & type::mask()]._data) : ((PAYLOAD*)NULL); }
#if __cplusplus > 199711L
                 bool operator==(std::nullptr_t) { return !_ready; }
                 bool operator!=(std::nullptr_t) { return _ready; }
                 explicit operator bool() { return _ready; }
#else
                 operator bool() { return _ready; }
#endif
                 bool operator!() const { return !_ready; }
                 void abandon() { _mq = NULL; }
                 ~MessageQueueReadHandle() {
                   if (_ready && _mq != NULL) { ++*_readcount; }
                 }
                 MessageQueueReadHandle(const MessageQueueReadHandle& other): _mq(other._mq), _ready(other._ready), _readcount(other._readcount) {
                   (const_cast<MessageQueueReadHandle&>(other))._ready = false;
                   (const_cast<MessageQueueReadHandle&>(other))._mq = NULL;
                   (const_cast<MessageQueueReadHandle&>(other))._readcount = NULL;
                 }
                 MessageQueueReadHandle& operator=(const MessageQueueReadHandle& other) {
                   if (this != &other) {
                     _mq = other._mq;
                     _ready = other._ready;
                     _readcount = other._readcount;
                     (const_cast<MessageQueueReadHandle&>(other))._ready = false;
                     (const_cast<MessageQueueReadHandle&>(other))._mq = NULL;
                     (const_cast<MessageQueueReadHandle&>(other))._readcount = NULL;
                   }
                   return *this;
                 }
                 private:
                 const type* _mq;
                 bool _ready;
                 volatile int64_t* _readcount;
               };
               friend class ::MessageQueueTest;
               void expectedHeader(MessageQueueHeader& h, const std::string &overrideName_ = "") const;
      private:
               // noncopyable (w/o boost dependency)
               MessageQueue(const MessageQueue&) = delete; 
               MessageQueue& operator=(const MessageQueue&) = delete;

    };

  template <class PAYLOAD, size_t SIZE_ELEMENTS>
    MessageQueue<PAYLOAD,SIZE_ELEMENTS>::MessageQueue(const std::string &overrideName_)
    {
      expectedHeader(_header, overrideName_);
    }
  template <class PAYLOAD, size_t SIZE_ELEMENTS>
    inline void MessageQueue<PAYLOAD,SIZE_ELEMENTS>::confirmHeader(const std::string &overrideName_) const {
      MessageQueueHeader header;
      expectedHeader(header, overrideName_);
      if (strncmp(header._typeCheck, _header._typeCheck, sizeof(header._typeCheck))!=0) {
        fprintf(stderr, "Type mismatch: %s vs %s\n", header._typeCheck, _header._typeCheck);
        throw std::runtime_error("type mismatch of queue");
      }
      if (header._lengthCheck != _header._lengthCheck) {
        fprintf(stderr, "Message queue type length doesn't match that in segment: %ld vs %ld\n",
            header._lengthCheck, _header._lengthCheck);
        throw std::runtime_error("message queue length mismatch");
      }
    }

  template <class PAYLOAD, size_t SIZE_ELEMENTS>
    inline void MessageQueue<PAYLOAD,SIZE_ELEMENTS>::expectedHeader(
        MessageQueue<PAYLOAD,SIZE_ELEMENTS>::MessageQueueHeader& header,
        const std::string &overrideName_) const {
      header._lengthCheck = sizeof(*this);
      if(overrideName_.empty()) {   // determine type name magically
        int status = 0;
        char* realname = abi::__cxa_demangle(typeid(*this).name(), 0, 0, &status);
        if (realname == NULL) {
          snprintf(header._typeCheck, sizeof(header._typeCheck), "%s", typeid(*this).name());
        } else {
          snprintf(header._typeCheck, sizeof(header._typeCheck), "%s", realname);
          free(realname);
        }
      } else {                      // otherwise use the supplied type name
                                    // we have to force a generated type name that will match later attempts to read
        std::string tn("MessageQueue<");
        tn += overrideName_ + ", " + std::to_string((long long unsigned int)(SIZE_ELEMENTS)) + "ul>";
        strncpy(header._typeCheck, tn.c_str(), sizeof(header._typeCheck) - 1);
      }
    }

  template <class PAYLOAD, size_t SIZE_ELEMENTS>
    typename MessageQueue<PAYLOAD,SIZE_ELEMENTS>::MessageQueueReadHandle MessageQueue<PAYLOAD,SIZE_ELEMENTS>::recv(volatile int64_t& readcount) const {
      return MessageQueueReadHandle(this, readcount);
    }

  template <class PAYLOAD, size_t SIZE_ELEMENTS>
    typename MessageQueue<PAYLOAD,SIZE_ELEMENTS>::MessageQueueWriteHandle MessageQueue<PAYLOAD,SIZE_ELEMENTS>::nextWriteSlot() {
      return MessageQueueWriteHandle(this);
    }
  template <class PAYLOAD, size_t SIZE_ELEMENTS>
    typename MessageQueue<PAYLOAD,SIZE_ELEMENTS>::LockedMessageQueueWriteHandle MessageQueue<PAYLOAD,SIZE_ELEMENTS>::nextWriteSlotLocked() {
      return LockedMessageQueueWriteHandle(this);
    }

} // namespace Salvo
#endif


