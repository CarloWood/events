// events -- C++ Core utilities
//
//! @file
//! @brief Definition of template classes ....
//
// Copyright (C) 2018 Carlo Wood.
//
// RSA-1024 0x624ACAD5 1997-01-26                    Sign & Encrypt
// Fingerprint16 = 32 EC A7 B6 AC DB 65 A6  F6 F6 55 DD 1C DC FF 61
//
// This file is part of ai-utils.
//
// ai-utils is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// ai-utils is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with ai-utils.  If not, see <http://www.gnu.org/licenses/>.

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
// A client without busy interface:
//
// class MyClient : public event::Client {
//   ~MyClient() { cancel_all_requests(); }
// };
//
// A client with one or more (N) busy interfaces:
//
// class MyClient : public event::BusyClient<N> {
//   ~MyClient() { cancel_all_requests(); }
// };

#pragma once

#include "debug.h"
#include "utils/AIRefCount.h"
#include "utils/NodeMemoryPool.h"
#include <deque>
#include <functional>
#include <array>
#include <atomic>
#include <thread>

namespace event {

template<class TYPE>
class Request;

// Event types.
template<class TYPE>
struct Types
{
  using request = Request<TYPE>;
  using request_ptr = boost::intrusive_ptr<request>;
  using callback = std::function<void(TYPE const&)>;
};

template<class TYPE> using request_handle = typename Types<TYPE>::request_ptr;

template<class TYPE>
class Request;

class BusyInterface
{
 private:
  void flush_events();          // Flush all queued events until the list is empty or the client is marked busy again.
                                // The latter can happen when one of the call back functions calls `is_busy()' for this interface.

  class QueuedEventBase
  {
    friend void BusyInterface::flush_events();
    virtual void retrigger() = 0;
  };

 public:
  // Queue the event "request / type".
  // This event will be handled as soon as `unset_busy' is called while m_busy_depth is 1.
  template<class TYPE>
  inline void queue(Request<TYPE>* request, TYPE const& type);

 private:
  template<class TYPE>
  class QueuedEvent final : public QueuedEventBase
  {
    using request_ptr_type = typename Types<TYPE>::request_ptr;
   private:
    request_ptr_type m_request;
    TYPE const m_type;
   private:
    friend void BusyInterface::queue<TYPE>(Request<TYPE>*, TYPE const&);
    QueuedEvent(Request<TYPE>* request, TYPE const& type) : m_request(request), m_type(type) { }
   private:
    void retrigger() override { m_request->rehandle(m_type); delete this; }
  };

 private:
  std::mutex m_mutex;                           // Mutex to protect the consistency of this class.
  std::atomic_uint m_busy_depth;                // Busy depth counter.  The client is busy when this is larger than zero.
  std::list<QueuedEventBase*> m_events;         // List with pointers to queued events that could not be handled at the moment the event
                                                // occurred because the client was busy.

 public:
  // Constructor
  BusyInterface() : m_busy_depth(0) { }

  // Increment busy depth counter, returns true if the previous value was 0,
  // which means that we are now allowed to call the callback. Otherwise
  // the event has to be queued by calling queue().
  bool set_busy()
  {
    DoutEntering(dc::notice, "BusyInterface::set_busy() [" << (void*)this << "]");
    return m_busy_depth.fetch_add(1) == 0;
  }

  // Decrement busy depth counter and flush all queued events, if any, when the interface becomes not busy.
  void unset_busy();
};

extern BusyInterface dummy_busy_interface;      // Just to have some address of type BusyInterface*.
static constexpr BusyInterface* const s_handled = &dummy_busy_interface;

template<class TYPE>
class Request
{
 private:
  std::atomic_int m_count;      // Reference counter for this object.
  std::atomic_int m_state;      // If the least signficant bit is set then the requester is
                                // about to be destroyed and the request should just be ignored.
                                // If the value is greater than 0 then at least one thread
                                // is inside handle() and the requester may not destruct (see intrusive_ptr_release below).
                                // If the value is less than 0 then the requester left intrusive_ptr_release
                                // and might destruct everything at any moment; therefore in that
                                // case we may no longer use m_busy_interface or m_callback.

 public:
  friend void intrusive_ptr_add_ref(Request* ptr)
  {
    ptr->m_count.fetch_add(1, std::memory_order_relaxed);
  }

  friend void intrusive_ptr_release(Request* ptr)
  {
    int old_count = ptr->m_count.fetch_sub(1, std::memory_order_release);
    if (old_count == 2)                                 // In this case the calling thread is about to destruct the object(s) that are needed for the callback.
    {
      // Mark that m_busy_interface and the objects needed m_callback are about to be invalidated by setting the least significant bit.
      ptr->m_state.fetch_sub(1);
      // Wait with leaving this function (and thus invalidating anything) until all threads left handle().
      while (ptr->m_state != -1)
        std::this_thread::yield();
    }
    if (old_count == 1)                                 // The last reference was just removed from Server<TYPE>::m_requests.
    {
      std::atomic_thread_fence(std::memory_order_acquire);
      delete ptr;
    }
  }

  using callback_type = typename Types<TYPE>::callback;

 private:
  BusyInterface* m_busy_interface;
  callback_type m_callback;

 public:
  Request(callback_type&& callback, BusyInterface* busy_interface = nullptr) :
      m_count(0), m_state(0), m_busy_interface(busy_interface), m_callback(std::move(callback)) { Dout(dc::notice, "Constructing Request [" << this << "]"); }
  ~Request() { Dout(dc::notice, "Destructing Request [" << this << "]"); }
  bool handle(TYPE const& type);
  void rehandle(TYPE const& type);
  void operator delete(void* ptr) { utils::NodeMemoryPool::static_free(ptr); }
};

inline void BusyInterface::unset_busy()
{
  DoutEntering(dc::notice, "BusyInterface::unset_busy() [" << (void*)this << "]");
  if (m_busy_depth == 1 && !m_events.empty())
    flush_events();
#ifdef CWDEBUG
  if (m_busy_depth == 0)
    DoutFatal(dc::core, "Calling unset_busy() more often than set_busy()");
#endif
  --m_busy_depth;
}

template<class TYPE>
void BusyInterface::queue(Request<TYPE>* request, TYPE const& type)
{
  QueuedEvent<TYPE>* queued_event = new QueuedEvent<TYPE>(request, type);
  AllocTag1(queued_event);
  m_events.push_back(queued_event);
}

template<class TYPE>
bool Request<TYPE>::handle(TYPE const& type)
{
  DoutEntering(dc::notice, "Request<" << libcwd::type_info_of<TYPE>().demangled_name() << ">::handle() with ref count = " << m_count << " and state = " << m_state);

  // This function is (only) called from Server::trigger because it was found in Server<TYPE>::m_requests.
  // Hence the ref count is at LEAST 1. While we're here that could go down from (say) 2 to 1, but if
  // it reaches 1 then it can never go up again: nobody can make copies of the boost::intrusive_ptr in
  // Server<TYPE>::m_requests. Therefore, if m_count becomes 1 then it is guaranteed that it will stay 1.
  //
  // However if m_count is larger than 1 then we will call m_callback(type), so we have to stop other
  // threads from destroying objects (including whatever m_busy_interface is pointing at) that are
  // needed for that to be allowed. The only mechanism that we have is that the user should guarantee
  // that they will destroy the last boost::intrusive_ptr to this object before destroying the busy
  // interface (if any) or any object that is needed for the callback.
  //
  // Therefore, the only way to stop a thread from destroying those objects is by blocking a thread that
  // tries to make the ref count unique after we checked for its uniqueness here.
  //
  // This is done with the m_state.

  if (!(m_state.fetch_add(2) & 1))
  {

    // At this point m_count can already be 1, in which case we'll just return.
    // It could also be 2 and be decreased to 1 by another thread (which after that would block
    // until we unlock m_unique_mutex), in which case we'd also return.

    if (std::atomic_load_explicit(&m_count, std::memory_order_relaxed) == 1)      // Is the server the only one left with a reference to this request?
      goto request_handled;             // Delete request.

    // This means that m_count wasn't already 1. It can still become 1 right here (or below),
    // but the thread doing so would block until we leave this scope (aka, until we called
    // m_callback(type) and returned from it. Note that normally a thread that destroys the
    // callback object should first set a flag causing the callback to immediately return,
    // before attempting to destroy it. So, it won't be blocked for any significant amount
    // of time.

    BusyInterface* bi = m_busy_interface;
    if (TYPE::one_shot)
    {
      // Make sure that there is a balance between the number of calls to request() and the number of calls to m_callback().
      if (bi == s_handled)
        goto request_handled;
      m_busy_interface = s_handled;     // Only one callback per Request object please.
    }

    // If there is no busy interface, just do the call back.
    if (!bi)
    {
      m_callback(type);
      m_state.fetch_sub(2);
      return false;                     // Keep request when it isn't one_shot.
    }

    // Otherwise mark busy interface as busy and do the callback when it wasn't already busy, otherwise queue the event.
    if (bi->set_busy())
      m_callback(type);
    else
      bi->queue(this, type);

    // Now that we did some work, lets check again if there is another thread that is possibly blocking
    // and about to destruct everything. If so, just leave and delete the request.
    if (std::atomic_load_explicit(&m_count, std::memory_order_relaxed) == 1)
      goto request_handled;             // Abort.

    bi->unset_busy();
  }

request_handled:
  m_state.fetch_sub(2);
  return true;                          // Keep request when it isn't one_shot.
}

template<class TYPE>
void Request<TYPE>::rehandle(TYPE const& type)
{
#ifndef CWDEBUG
  if (!m_busy_interface || !m_busy_interface.is_busy())
    DoutFatal(dc::core, "Calling event::Request<TYPE>::rehandle() without busy BusyInterface.");
#endif
  m_callback(type);
}

template<class TYPE>
class Server
{
  using request_ptr_type = typename Types<TYPE>::request_ptr;
  std::vector<request_ptr_type> m_requests;
  utils::NodeMemoryPool m_request_pool;

 public:
  Server() : m_request_pool(64, sizeof(Request<TYPE>)) { }

  // Passing directly a std::function.
  [[nodiscard]] boost::intrusive_ptr<Request<TYPE>>& request(std::function<void(TYPE const&)>&& callback)
  {
    return m_requests.emplace_back(new (m_request_pool) Request<TYPE>(std::move(callback)));
  }

  // Passing directly a std::function and busy interface.
  [[nodiscard]] boost::intrusive_ptr<Request<TYPE>>& request(std::function<void(TYPE const&)>&& callback, BusyInterface& busy_interface)
  {
    return m_requests.emplace_back(new (m_request_pool) Request<TYPE>(std::move(callback), &busy_interface));
  }

  // Non-const client.
  template<class CLIENT, typename... Args>
  [[nodiscard]] boost::intrusive_ptr<Request<TYPE>>& request(CLIENT& client, void (CLIENT::*cb)(TYPE const&, Args...), Args... args)
  {
    Dout(dc::notice, "Calling Server::request(" << libcwd::type_info_of<CLIENT>().demangled_name() << "&, " << libcwd::type_info_of(cb).demangled_name() << ", ...)");
    using namespace std::placeholders;
    return m_requests.emplace_back(new (m_request_pool) Request<TYPE>(std::bind(cb, &client, _1, args...)));
  }

  template<class CLIENT, typename... Args>
  [[nodiscard]] boost::intrusive_ptr<Request<TYPE>>& request(CLIENT& client, void (CLIENT::*cb)(TYPE const&, Args...), BusyInterface& busy_interface, Args... args)
  {
    Dout(dc::notice, "Calling Server::request(" << libcwd::type_info_of<CLIENT>().demangled_name() << "&, " << libcwd::type_info_of(cb).demangled_name() << ", BusyInterface& [" << (void*)&busy_interface << "], ...)");
    using namespace std::placeholders;
    return m_requests.emplace_back(new (m_request_pool) Request<TYPE>(std::bind(cb, &client, _1, args...), &busy_interface));
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
  [[nodiscard]] boost::intrusive_ptr<Request<TYPE>>& request(CLIENT const& client, void (CLIENT::*cb)(TYPE const&, Args...) const, Args... args)
  {
    Dout(dc::notice, "Calling Server::request(" << libcwd::type_info_of<CLIENT>().demangled_name() << " const&, " << libcwd::type_info_of(cb).demangled_name() << ", ...)");
    using namespace std::placeholders;
    return m_requests.emplace_back(new (m_request_pool) Request<TYPE>(std::bind(cb, &client, _1, args...)));
  }

  void trigger(TYPE const& type);
};

template<class TYPE>
void Server<TYPE>::trigger(TYPE const& type)
{
  DoutEntering(dc::notice, "event::Server<" << libcwd::type_info_of<TYPE>().demangled_name() << ">::trigger(" << type << ")");
  if (TYPE::one_shot)
  {
    for (auto& request : m_requests)
      request->handle(type);
    m_requests.clear();
  }
  else
    m_requests.erase(
        std::remove_if(m_requests.begin(),
                       m_requests.end(),
                       [&type](request_ptr_type const& request)
                       { return request->handle(type); }
                      ),
        m_requests.end());
}

} // namespace event
