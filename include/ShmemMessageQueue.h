#ifndef __SHMEMMESSAGEQUEUE_H__
#define __SHMEMMESSAGEQUEUE_H__

#include "MessageQueue.hpp"
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <grp.h>

#define MQFATAL(...) do { \
  fprintf(stderr, ##__VA_ARGS__); \
  exit(-1); \
} while (0)

// Shared memory-based message queue creation helper.
// Initial call results in creation, subsequent calls result in attachment.
namespace Salvo {
  namespace ShmemMessageQueue {
#if __cplusplus > 199711L
    template <class PAYLOAD, size_t SIZE_ELEMENTS = MessageQueueTraits::defaultSize> using queueType = MessageQueue<PAYLOAD, SIZE_ELEMENTS>;
#endif
    template<class PAYLOAD, size_t SIZE_ELEMENTS = MessageQueueTraits::defaultSize>
      struct queueTypeDef
      {
        typedef MessageQueue<PAYLOAD, SIZE_ELEMENTS> type;
      };

    // If writer_ is set to true, you can write to the queue as well as read to it.
    // Otherwise, only reading is allowed.
    template <class PAYLOAD, size_t SIZE_ELEMENTS = MessageQueueTraits::defaultSize>
      static typename queueTypeDef<PAYLOAD, SIZE_ELEMENTS>::type *create(const std::string &name_, bool writer_ = false, const std::string &nameOverride_ = "");

    template <class PAYLOAD, size_t SIZE_ELEMENTS>
      typename queueTypeDef<PAYLOAD, SIZE_ELEMENTS>::type *create(const std::string &name_, bool writer_, const std::string &nameOverride_) {
        typedef typename queueTypeDef<PAYLOAD, SIZE_ELEMENTS>::type qType;

        int fd = -1;
        int prot = writer_ ? (PROT_READ | PROT_WRITE) : PROT_READ;
        int oflag = O_RDWR;
        int mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP;
        bool newSeg = true;

        // first attempt to create
        if(-1 == (fd = shm_open(name_.c_str(), oflag | O_CREAT | O_EXCL, mode)))
        {
          if(EEXIST == errno)         // that's cool, re-attach as a non-creator
          {
            newSeg = false;
            if(-1 == (fd = shm_open(name_.c_str(), oflag, mode)))
            {
              MQFATAL("failure in non-exclusive creation attempt shm_open of '%s', errno = %d\n", name_.c_str(), errno);
            }
          }
          else
          {
            MQFATAL("failure in exclusive creation attempt shm_open of '%s', errno = %d\n", name_.c_str(), errno);
          }
        }
        else
        {
          if(-1 == ftruncate(fd, sizeof(qType)))
          {
            MQFATAL("failure in ftruncate after creation of shared memory segment '%s', errno = %d\n", name_.c_str(), errno);
          }
          struct group* shmem_user = getgrnam("shmem_user");
          if (shmem_user != NULL) {
            int s = fchown(fd, uid_t(-1), shmem_user->gr_gid);
            if (s == -1) { fprintf(stderr, "fchown failed '%s', errno = %d\n", name_.c_str(), errno); }
          }
        }

        qType *queue = NULL;

        if(newSeg)                    // need to temporarily attach as writeable to create object in memory
        {
          // by now we have a valid fd we can use as a handle
          void *addr = mmap(0, sizeof(qType), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
          if(reinterpret_cast<void *>(-1) == addr)
          {
            MQFATAL("failure in mmap after attaching to shared memory segment '%s', errno = %d\n", name_.c_str(), errno);
          }

          queue = new (addr) qType(nameOverride_); // creation case
          munmap(addr, sizeof(qType));
        }

        // by now we have a valid fd we can use as a handle
        int mmapFlags = MAP_SHARED;
        if (getenv("RUNNING_WITHOUT_RIGHTS") == NULL) {
          mmapFlags |= MAP_LOCKED;
        }
        void *addr = mmap(0, sizeof(qType), prot, mmapFlags, fd, 0);
        if(reinterpret_cast<void *>(-1) == addr)
        {
          MQFATAL("failure in mmap after attaching to shared memory segment '%s', errno = %d\n", name_.c_str(), errno);
        } else {
          // pretouch all pages
          volatile char c = 0;
          for (size_t i = 0; i < sizeof(qType); i += (4 * 1024)) {
            c += ((const char*)addr)[i];
          }
          fprintf(stderr, "Touched all memory (sum == %d)\n", int(c));
        }

        queue = static_cast<qType *>(addr); // now we're attached

        // lastly, confirm type of queue (guaranteed to work if we created it)
        int64_t startOfCheck = time(NULL);
        do {
          try {
            queue->confirmHeader(nameOverride_);
          } catch (const std::runtime_error& err) {
            if (time(NULL) > startOfCheck + 2) {
              throw;
            } else {
              ::usleep(1000*250);
              continue;
            }
          }
          break;
        } while (1);

        return queue;
      }

  }
}
#endif
