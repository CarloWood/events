/**
 * events -- C++ Core utilities
 *
 * @file
 * @brief Definition of template classes ....
 *
 * @Copyright (C) 2018  Carlo Wood.
 *
 * RSA-1024 0x624ACAD5 1997-01-26                    Sign & Encrypt
 * Fingerprint16 = 32 EC A7 B6 AC DB 65 A6  F6 F6 55 DD 1C DC FF 61
 *
 * This file is part of events.
 *
 * Events is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Events is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with events.  If not, see <http://www.gnu.org/licenses/>.
 */

//============================================================================
//
//                              E V E N T S
//
// Program flow
// ------------
//
// The EVENT-CLIENT requests an EVENT-TYPE by calling the request() method of
// the corresponding EVENT-SERVER. When that event occurs, the EVENT-GENERATOR
// passes the EVENT-DATA to the EVENT-SERVER, which passes it on to all EVENT-CLIENTs
// that requested the event.
//
// If a BUSY-INTERFACE was passed along with the request, then the EVENT-SERVER
// probes this BUSY-INTERFACE to see if the EVENT-CLIENT is busy. If the client
// is busy, the busy interface queues the event and EVENT-DATA till it is not
// busy anymore. Otherwise the event is passed directly to the client.
//

// Usage
// -----
//
#ifdef EXAMPLE_CODE     // undefined
// Assume each event comes with certain values that need to
// be passed to the callback functions.
struct MyEventData {
  int x;
  int y;
};

// This class may be shared between multiple events, for example:
class MyEventType1 : public MyEventData {
};

class MyEventType2 : public MyEventData {
};

// Create an event server for each event type.
events::Server<MyEventType1> server1;
events::Server<MyEventType2> server2;

// Whenever the events happen, call their respective trigger function.
  ...
  server1.trigger(my_event_data);

// Assume you have an object MyClient that wants to receive events
// of both types, but only be called by one thread at a time.
class MyClient {
  void callback1(MyEventData const& data);      // To be called when event 1 happens.
  void callback2(MyEventData const& data);      // To be called when event 2 happens.

  events::BusyInterface m_busy_interface;       // The busy interface for this object.
  events::RequestHandle<MyEventType1> m_handle1;
  events::RequestHandle<MyEventType2> m_handle1;

  ~MyClient() {
    // Always call cancel() before destructing the handle, the busy interface, or
    // anything else that is needed for the callback function to be well-behaved.
    m_handle1.cancel();
    m_handle2.cancel();
  }
};

// Request the events with the servers.
  MyClient client;
  ...
  m_handle1 = server1.request(client, &MyClient::callback1, m_busy_interface);
  m_handle2 = server2.request(client, &MyClient::callback2, m_busy_interface);
#endif // EXAMPLE_CODE

#pragma once

#include "debug.h"
#include "utils/NodeMemoryPool.h"
#include "utils/macros.h"
#include <functional>
#include <condition_variable>
#include <atomic>
#include <deque>

#if defined(CWDEBUG) && !defined(DOXYGEN)
NAMESPACE_DEBUG_CHANNELS_START
extern channel_ct events;
NAMESPACE_DEBUG_CHANNELS_END
#endif

namespace events {

class QueuedEventBase;

struct BusyInterface
{
 private:
  std::atomic_uint m_busy_depth;
  std::mutex m_events_mutex;
  std::deque<QueuedEventBase const*> m_events;

 public:
  BusyInterface() : m_busy_depth(0) { }
  bool set_busy() { return m_busy_depth.fetch_add(1) == 0; }
  bool unset_busy() { return m_busy_depth.fetch_sub(1) == 1; }

  void push(QueuedEventBase const* new_queued_event)
  {
    std::lock_guard<std::mutex> lock(m_events_mutex);
    m_events.push_back(new_queued_event);
  }

  QueuedEventBase const* pop()
  {
    QueuedEventBase const* queued_event = nullptr;
    std::lock_guard<std::mutex> lock(m_events_mutex);
    if (!m_events.empty())
    {
      queued_event = m_events.front();
      m_events.pop_front();
    }
    return queued_event;
  }
};

template<typename TYPE>
class Server;

template<typename TYPE>
class Request
{
  static constexpr int s_cancel_marker = 0x10000;

 private:
  std::atomic_int m_handle_count;               // The number of threads that are currently handling this request. Possibly decremented by s_cancel_marker when the request is canceled.
  std::mutex m_cancel_mutex;                    // Synchronization mutex for the condition variable.
  std::condition_variable m_cancel_cv;          // A condition variable used to stop a thread that called cancel() from leaving until all other threads are done with the request.

  // A singly linked list of Request objects that registered with Server<TYPE>.
  // Concurrent access is protected by Server<TYPE>::m_request_list_mutex.
  friend class Server<TYPE>;
  Request* m_next;

#ifdef CWDEBUG
  bool silent_;                                 // Silence debug output during the callback.
#endif

 protected:
  std::function<void(TYPE const&)> m_callback;

 public:
  Request(std::function<void(TYPE const&)>&& callback) : m_handle_count(0), m_callback(std::move(callback))
  {
    Dout(dc::events, "Constructing Request [" << this << "]");
  }

  virtual ~Request()
  {
    Dout(dc::events, "Destructing Request [" << this << "]");
    // Call cancel() on the RequestHandle before destructing it.
    ASSERT(m_handle_count < 0);
  }

  // Increment m_handle_count atomically by 1 if and only if the Request wasn't canceled (m_handle_count is negative).
  // Return 0 if the request needs handling (wasn't canceled), or
  // return -1 if the request was canceled and no other thread is handling it, return 1 otherwise.
  int start_handling()
  {
    int handle_count = m_handle_count;
    do
    {
      // Was this request canceled?
      if (AI_UNLIKELY(handle_count < 0))
        return (handle_count == -s_cancel_marker) ? -1 : 1;
    }
    while (!m_handle_count.compare_exchange_weak(handle_count, handle_count + 1));
    return 0;
  }

  // Subtract 1 from m_handle_count. If this was the last thread to leave the area
  // between start_handling and stop_handling then wake up the thread that called
  // cancel and is waiting there.
  void stop_handling()
  {
    if (AI_UNLIKELY(m_handle_count.fetch_sub(1) == 1 - s_cancel_marker))  // Canceled and no threads left?
    {
      // Don't call notify_one() before the thread inside cancel() entered wait() and unlocked the mutex.
      m_cancel_mutex.lock();
      m_cancel_mutex.unlock();
      // Wake it up.
      m_cancel_cv.notify_one();
    }
  }

  virtual void handle(TYPE const& data)
  {
    DoutEntering(dc::events(!silent_), "[" << (void*)this << "]::handle(" << data << ")");
    // Without a busy interface, the callback itself is responsible for being thread-safe,
    // or knowing that this event is only generated by a single thread.
    Debug(if (silent_) libcw_do.off());
    m_callback(data);
    Debug(if (silent_) libcw_do.on());
  }

  void cancel();

#ifdef CWDEBUG
  bool is_canceled() const { return m_handle_count == -s_cancel_marker; }
  void debug_set_silent(bool silent)
  {
    DoutEntering(dc::events, "Request<" << type_info_of<TYPE>().demangled_name() << ">::debug_set_silent(" << std::boolalpha << silent << ")");
    silent_ = silent;
  }
#endif
};

template<typename TYPE>
class RequestWithBI final : public Request<TYPE>
{
  static utils::NodeMemoryPool s_queued_event_memory_pool;

 private:
  BusyInterface* m_busy_interface;      // Busy interface to be used for this request.

 public:
  RequestWithBI(std::function<void(TYPE const&)>&& callback, BusyInterface* busy_interface) :
      Request<TYPE>(std::move(callback)), m_busy_interface(busy_interface) { Dout(dc::events, "Constructing RequestWithBI [" << this << "]"); }

  void handle(TYPE const& data) override;

  void rehandle(TYPE const& data)
  {
    DoutEntering(dc::events, "[" << (void*)this << "]::rehandle(" << data << ")");
    this->m_callback(data);
  }
};

// RequestHandle is NOT thread safe!
// It can only be moved, and cancel() may only be called once (by a single thread obviously).
// Moreover, cancel() must be called before any objects are destructed that are needed for
// the callback of this request.
template<typename TYPE>
class RequestHandle
{
 private:
  friend class Server<TYPE>;
  Request<TYPE>* m_request;

 public:
  RequestHandle() : m_request(nullptr) { }
  ~RequestHandle() { /* Call cancel() before destructing anything that is needed for the callback */ ASSERT(!m_request || m_request->is_canceled()); }
  RequestHandle(Request<TYPE>* request) : m_request(request) { }
  RequestHandle(RequestHandle&& orig) : m_request(orig.m_request) { orig.m_request = nullptr; }
  RequestHandle& operator=(RequestHandle&& orig) { m_request = orig.m_request; orig.m_request = nullptr; return *this; }
  void cancel();
#ifdef CWDEBUG
  void debug_set_silent(bool silent = true) { m_request->debug_set_silent(silent); }
#endif
};

class QueuedEventBase
{
 public:
  virtual ~QueuedEventBase() { }
  virtual void rehandle() const = 0;
  void operator delete(void* ptr, size_t) { utils::NodeMemoryPool::static_free(ptr); }
};

template<typename TYPE>
struct QueuedEvent final : public QueuedEventBase
{
  RequestWithBI<TYPE>* m_request;
  TYPE m_data;

  QueuedEvent(RequestWithBI<TYPE>* request, TYPE const& data) : m_request(request), m_data(data) { }

 private:
  void rehandle() const override;
};

template<typename TYPE>
class Server
{
 public:
  using event_type_type = TYPE;

 private:
  std::mutex m_request_list_mutex;              // Locked when changing any Request<TYPE>* that is part of m_request_list,
  Request<TYPE>* m_request_list;                // so that integrity of m_request_list is guaranteed when the mutex is not locked.
  utils::NodeMemoryPool m_request_memory_pool;  // Used to allocate both Request<TYPE> and RequestWithBI<TYPE> objects.

  // Insert the newly allocated new_request in the front of the list.
  void push_front(Request<TYPE>* new_request)
  {
    std::lock_guard<std::mutex> lock(m_request_list_mutex);
    // Read current head.
    new_request->m_next = m_request_list;
    m_request_list = new_request;
  }

  // Erase the node following parent, (*parent)->m_next, which must exist.
  // Also, m_request_list_mutex must already be locked!
  void delink(Request<TYPE>*& next)
  {
    next = next->m_next;
  }

 public:
  Server() : m_request_list(nullptr), m_request_memory_pool(64, sizeof(RequestWithBI<TYPE>)) { }

  void trigger(TYPE const& data);

  // Passing directly a std::function.
  [[nodiscard]] RequestHandle<TYPE> request(std::function<void(TYPE const&)>&& callback)
  {
    Dout(dc::events|continued_cf, "Calling Server::request(std::function<void(" << libcwd::type_info_of<TYPE>().demangled_name() << " const&)>&&) ");
    Request<TYPE>* request = new (m_request_memory_pool) Request<TYPE>(std::move(callback));
    push_front(request);
    Dout(dc::finish, "--> [" << (void*)request << ']');
    return request;
  }

  // Passing directly a std::function and busy interface.
  [[nodiscard]] RequestHandle<TYPE> request(std::function<void(TYPE const&)>&& callback, BusyInterface& busy_interface)
  {
    Dout(dc::events|continued_cf, "Calling Server::request(std::function<void(" << libcwd::type_info_of<TYPE>().demangled_name() << " const&)>&&, BusyInterface& [" << (void*)&busy_interface << "]) ");
    Request<TYPE>* request = new (m_request_memory_pool) RequestWithBI<TYPE>(std::move(callback), &busy_interface);
    push_front(request);
    Dout(dc::finish, "--> [" << (void*)request << ']');
    return request;
  }

  // Non-const client.
  template<class CLIENT, typename... Args>
  [[nodiscard]] RequestHandle<TYPE> request(CLIENT& client, void (CLIENT::*cb)(TYPE const&, Args...), Args... args)
  {
    Dout(dc::events|continued_cf, "Calling Server::request(" << libcwd::type_info_of<CLIENT>().demangled_name() << "&, " << libcwd::type_info_of(cb).demangled_name() << ", ...) ");
    using namespace std::placeholders;
    Request<TYPE>* request = new (m_request_memory_pool) Request<TYPE>(std::bind(cb, &client, _1, args...));
    push_front(request);
    Dout(dc::finish, "--> [" << (void*)request << ']');
    return request;
  }

  template<class CLIENT, typename... Args>
  [[nodiscard]] RequestHandle<TYPE> request(CLIENT& client, void (CLIENT::*cb)(TYPE const&, Args...), BusyInterface& busy_interface, Args... args)
  {
    Dout(dc::events|continued_cf, "Calling Server::request(" << libcwd::type_info_of<CLIENT>().demangled_name() << "&, " << libcwd::type_info_of(cb).demangled_name() << ", BusyInterface& [" << (void*)&busy_interface << "], ...) ");
    using namespace std::placeholders;
    Request<TYPE>* request = new (m_request_memory_pool) RequestWithBI<TYPE>(std::bind(cb, &client, _1, args...), &busy_interface);
    push_front(request);
    Dout(dc::finish, "--> [" << (void*)request << ']');
    return request;
  }

  // Const client.
  //
  // A callback to a const client seems not practical: isn't the idea of an event
  // call back that some action has to be taken; the client likely needs to remember
  // that the event happened. Nevertheless in unforeseen cases one might want to
  // call a const member function, for which the above template argument deduction
  // will fail. And when adding a function that accepts a const member function
  // there is no need any more that client is non-const either.
  template<class CLIENT, typename... Args>
  [[nodiscard]] RequestHandle<TYPE> request(CLIENT const& client, void (CLIENT::*cb)(TYPE const&, Args...) const, Args... args)
  {
    Dout(dc::events|continued_cf, "Calling Server::request(" << libcwd::type_info_of<CLIENT>().demangled_name() << " const&, " << libcwd::type_info_of(cb).demangled_name() << ", ...) ");
    using namespace std::placeholders;
    Request<TYPE>* request = new (m_request_memory_pool) Request<TYPE>(std::bind(cb, &client, _1, args...));
    push_front(request);
    Dout(dc::finish, "--> [" << (void*)request << ']');
    return request;
  }

  template<class CLIENT, typename... Args>
  [[nodiscard]] RequestHandle<TYPE> request(CLIENT const& client, void (CLIENT::*cb)(TYPE const&, Args...) const, BusyInterface& busy_interface, Args... args)
  {
    Dout(dc::events|continued_cf, "Calling Server::request(" << libcwd::type_info_of<CLIENT>().demangled_name() << " const&, " << libcwd::type_info_of(cb).demangled_name() << ", BusyInterface& [" << (void*)&busy_interface << "], ...) ");
    using namespace std::placeholders;
    Request<TYPE>* request = new (m_request_memory_pool) RequestWithBI<TYPE>(std::bind(cb, &client, _1, args...), &busy_interface);
    push_front(request);
    Dout(dc::finish, "--> [" << (void*)request << ']');
    return request;
  }
};

template<typename TYPE>
void RequestHandle<TYPE>::cancel()
{
  // Only call cancel() once.
  ASSERT(m_request);
  m_request->cancel();
  m_request = nullptr;
}

template<typename TYPE>
void Server<TYPE>::trigger(TYPE const& data)
{
  if (TYPE::one_shot)
  {
    Request<TYPE>* head;
    {
      // If something is added to the list after we read head, then it was simply too late.
      std::lock_guard<std::mutex> lock(m_request_list_mutex);
      head = m_request_list;
      m_request_list = nullptr;
    }
    // Because all requests are now entirely delinked from the Server,
    // there is no need to lock the m_request_list_mutex anymore.
    Request<TYPE>* request = head;
    while (request)
    {
      request->handle(data);
      request = request->m_next;
    }
    // Return request memory to the memory pool.
    request = head;
    while (request)
    {
      Request<TYPE>* next = request->m_next;
      m_request_memory_pool.free(request);
      request = next;
    }
  }
  else
  {
    Request<TYPE>** next = &m_request_list;     // Let *next point to the first object.
    std::unique_lock<std::mutex> lock(m_request_list_mutex);
    for (;;)
    {
      Request<TYPE>* request;

      // Find and stop the next request object from being deleted (or delink it if that fails).
      int state;
      while ((request = *next) &&
             AI_UNLIKELY((state = request->start_handling())))  // -1: Canceled and no thread is handling the request.
                                                                //  0: Not canceled.
                                                                //  1: Canceled but one or more threads are handling the request.
      {
        // request is canceled.
        if (state == -1)
        {
          delink(*next);
          m_request_memory_pool.free(request);
        }
        else
          next = &request->m_next;
      }
      if (!request)
        break;

      lock.unlock();
      // While a thread is here, start_handling() was called at least once on this request
      // causing other threads that call start_handling() to never get -1 and therefore never
      // to delink and free this request object. start_handling() can only ever start to
      // return -1 again after we called stop_handling(), but at that point we own m_request_list_mutex
      // again, so that still no other thread can be delinking this request object.
      request->handle(data);
      lock.lock();

      // Allow this request object to be delinked and freed as soon as we unlock m_request_list_mutex.
      // This call might cause the associated RequestHandle and any object needed for the callback
      // to be immediately destructed (by another thread, currently blocked in cancel()), but not
      // the Request object itself.
      request->stop_handling();
      next = &request->m_next;
    }
  }
}

template<typename TYPE>
void RequestWithBI<TYPE>::handle(TYPE const& data)
{
  DoutEntering(dc::events, "RequestWithBI<" << libcwd::type_info_of<TYPE>().demangled_name() << ">::handle(" << data << ") [" << (void*)this << "]");
  // Atomically increment the "busy counter" of the busy interface.
  if (!m_busy_interface->set_busy())
  {
    Dout(dc::events, "Queuing event because busy interface is busy.");
    QueuedEventBase const* new_queued_event = new (s_queued_event_memory_pool) QueuedEvent<TYPE>(this, data);
    m_busy_interface->push(new_queued_event);
  }
  else
    this->m_callback(data);
  // Atomically decrement the "busy counter" of the busy interface.
  while (m_busy_interface->unset_busy())        // Exit this function if there are one or more other threads above us (let the last thread to get here handle the queue).
  {
    Dout(dc::events, "Unset_busy returned true: this thread is responsible for emptying the queue.");
    QueuedEventBase const* queued_event = m_busy_interface->pop();
    if (!queued_event)
      break;                                    // No events left in the queue: we're done.
    Dout(dc::events|continued_cf, "Processing one event from the queue... ");
    if (m_busy_interface->set_busy())           // After this set_busy() call we need to return to the top of the while loop of course, to call unset_busy().
    {
      queued_event->rehandle();
      delete queued_event;
    }
    else
    {
      // If rehandling failed then the queued event needs to be put back into the queue.
      // Note that in this case the call to unset_busy() above is likely to return false
      // and we won't retry to process this same event again.
      // Put the event back in the front of the queue so that the thread that caused
      // rehandle to fail will process this event first once it is done.
      m_busy_interface->push(queued_event);
    }
    Dout(dc::finish, "done");
  }
}

template<typename TYPE>
void QueuedEvent<TYPE>::rehandle() const
{
  m_request->rehandle(m_data);
}

template<typename TYPE>
void Request<TYPE>::cancel()
{
  // Lock m_cancel_mutex before (possibly) making the condition (m_handle_count reached -s_cancel_marker) true.
  if (m_handle_count.fetch_sub(s_cancel_marker) > 0)    // Are there still threads handling this request?
  {
    // Wait until all threads finished handling this request (called stop_handling()).
    std::unique_lock<std::mutex> lock(m_cancel_mutex);
    m_cancel_cv.wait(lock, [this]{ return m_handle_count == -s_cancel_marker; });
  }
}

// Instantiate a per-TYPE memory pool for queued events.
template<typename TYPE>
utils::NodeMemoryPool RequestWithBI<TYPE>::s_queued_event_memory_pool(32, sizeof(QueuedEvent<TYPE>));

} // namespace events
