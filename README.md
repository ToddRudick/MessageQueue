# MessageQueue

Header only C++11 library for fast in-process or shared-memory Message queues. Assumption is that a reader will copy or process data faster than the
writer can loop. For very high-capacity producer/consumers, readers should occasionally check for falling behind & skip/drop to catch up (which in practice doesn't happen in prod code if queues are big enough, but still could happen in testing / debugging / etc environments)

To use in-process:

```

  using namespace Salvo;
  struct NODE {
    char data[256];
    int i;
  };
  MessageQueue<NODE> mq;
  { // to write
    auto wrt = mq.nextWriteSlot(); /* add "Locked" to use spin-lock synchronization with other writers. Reader/Writer sync is lockless
                                      Reader will not pick this up until object goes out of scope (data is copied) */
    snprintf(wrt->data, sizeof(wrt->data), "Hello World");
    // wrt.abandon(); // to not write anything.
  } 
  { // to read
    int64_t readcount = 0; /* normally one would set to mq.writeCount() on startup. "0" will hang on restart if the queue has already been looped
                            * so if one wants to replay a bit, use std::max(mq.writeCount() - mq.capacity()/2, 0);
    auto msg = mq.recv(readcount);
    if (msg) {
      // you can now access msg->data, msg->i, *msg, etc. However this object is directly in the queue & can eventually get overridden by a writer so should be copied if not
      // used immediately
    }
  }
  
```    
    
To use an an IPC mechanism simply create the MessageQueue object pointer with ShmemMessageQueue::create
