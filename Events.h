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
#include "utils/macros.h"
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
  void pop_event();     // Flush all queued events until the list is empty or the client is marked busy again.
                        // The latter can happen when one of the call back functions calls `is_busy()' for this interface.

  class QueuedEventBase
  {
    friend void BusyInterface::pop_event();
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

  // W.I.P.
  bool unset_busy()
  {
    DoutEntering(dc::notice, "BusyInterface::unset_busy() [" << (void*)this << "]");
    // Obviously a thread should only call unset_busy() after first calling set_busy().
    // Hence, it should be impossible that m_busy_depth is zero here.
    ASSERT(m_busy_depth > 0);
    return m_busy_depth.fetch_sub(1) == 1;
  }

};

extern BusyInterface dummy_busy_interface;      // Just to have some address of type BusyInterface*.
static constexpr BusyInterface* const s_handled = &dummy_busy_interface;

template<class TYPE>
class Request
{
 private:
  static constexpr int destructing = 0x1;                               // If this bit is set in m_count then m_busy_interface and m_callback are about to be invalidated.
                                                                        // Calls to handle() must return immediately in that case in order to avoid unnecessary stalling
                                                                        // in intrusive_ptr_release.

  static constexpr int handle_count_shift = 1;                          // The handle count starts at the next bit.
  static constexpr int handle_count_unit = 1 << handle_count_shift;     // The next ref_count_shift - handle_count_shift bits are used to count the number of threads
                                                                        // inside handle().
  static constexpr int ref_count_shift = 16;
  static constexpr int ref_count_unit = 1 << ref_count_shift;           // The remaining bits are used as reference counter of this object.

  static constexpr int destructing_unique = ref_count_unit | destructing;

  std::atomic_int m_state;

  // If the least signficant bit is set then the requester is
  // about to be destroyed and the request should just be ignored.
  static bool is_destructing(int state) { return state & destructing; }
  static bool is_unique(int state) { return state >> ref_count_shift == 1; }
  static int ref_count(int state) { return state >> ref_count_shift; }

  struct QueueGuard
  {
    bool need_pop_front;
    Request<TYPE>* self;
    QueueGuard(Request<TYPE>* self_) : need_pop_front(false), self(self_) { }
    ~QueueGuard() { pop_front(); }
    void pop_front()
    {
      if (AI_UNLIKELY(need_pop_front))
      {
        std::lock_guard<std::mutex> lock(self->m_events_mutex);
        self->m_events.pop_front();
      }
    }
  };

 public:
  friend void intrusive_ptr_add_ref(Request* ptr)
  {
    ptr->m_state.fetch_add(ref_count_unit, std::memory_order_relaxed);
  }

  friend void intrusive_ptr_release(Request* ptr)
  {
    int prev_state = ptr->m_state.fetch_sub(ref_count_unit, std::memory_order_release);
    int new_ref_count = ref_count(prev_state) - 1;
    if (new_ref_count == 1)             // In this case the calling thread is about to destruct the object(s) that are needed for the callback.
    {
      // Mark that m_busy_interface and the objects needed m_callback are about to be invalidated by setting the least significant bit.
      ptr->m_state.fetch_or(destructing);
      // Wait with leaving this function (and thus invalidating anything) until all threads left handle().
      int count = 0;
      while (ptr->m_state != destructing_unique)        // This loops until handle_count == 0.
      {
        if (++count >= 200)
        {
          Dout(dc::warning(count % 200 == 0), "event::intrusive_ptr_release(" << type_info_of(ptr).demangled_name() << ") is hanging!");
          // Are you causing this event to happen from within the callback of such event? That is not possible for non-one-shot events!
          ASSERT(count < 1000);
          std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        std::this_thread::yield();
      }
    }
    else if (new_ref_count == 0)        // The last reference was just removed from Server<TYPE>::m_requests.
    {
      ASSERT(ptr->m_state == destructing);
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
      m_state(0), m_busy_interface(busy_interface), m_callback(std::move(callback)) { Dout(dc::notice, "Constructing Request [" << this << "]"); }
  ~Request() { Dout(dc::notice, "Destructing Request [" << this << "]"); }
  bool handle(TYPE const& type);
  void rehandle(TYPE const& type);
  void operator delete(void* ptr) { utils::NodeMemoryPool::static_free(ptr); }
};

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
  DoutEntering(dc::notice, "Request<" << libcwd::type_info_of<TYPE>().demangled_name() << ">::handle() with state = " << std::hex << m_state);

  struct SafeHandleCounter
  {
    std::atomic_int& m_state;
    int m_prev_state;
    SafeHandleCounter(std::atomic_int& state) : m_state(state), m_prev_state(m_state.fetch_add(handle_count_unit)) { }
    ~SafeHandleCounter() { m_state.fetch_sub(handle_count_unit); }
    operator int() const { return m_prev_state; }
  };

  // Increment m_state within this scope and remember the previous state.
  SafeHandleCounter prev_state(m_state);

  // Do nothing when the busy interface and/or the object(s) that are needed for m_callback to remain valid are about to be destructed
  // and/or the Server object is the only object that still has a reference to this Request object (which means the request was
  // cancelled).
  if (!is_destructing(prev_state) && !is_unique(prev_state))
  {
    BusyInterface* bi = m_busy_interface;
    if (TYPE::one_shot)
    {
      // Make sure that there is a balance between the number of calls to request() and the number of calls to m_callback().
      if (bi == s_handled)
        return true;
      m_busy_interface = s_handled;     // Only one callback per Request object please.
    }

    // If there is no busy interface, just do the call back.
    if (!bi)
    {
      m_callback(type);                 // Multiple threads might call this concurrently.
      return false;                     // Keep request when it isn't one_shot.
    }

    // Otherwise mark busy interface as busy and do the callback when it wasn't already busy, otherwise queue the event.
    if (bi->set_busy())
      m_callback(type);                 // One one thread at a time will call this.
    else
      bi->queue(this, type);

    // Now that we did some work, lets check again if there is another thread that is possibly blocking
    // and about to destruct everything. If so, just leave and delete the request. In that case we leave
    // the busy interface marked as 'busy', but that shouldn't have any negative effect since we should
    // nobody should be using the busy interface anymore as soon as the last thread exits this block;
    // hence the only possible effect is that another thread won't call pop_event() below while otherwise
    // it would have. This is actually a good thing.
    if (is_destructing(std::atomic_load_explicit(&m_state, std::memory_order_relaxed)))
      return true;                      // Abort.

    // Process queued events.
    // TODO: W.I.P.
    bi->unset_busy();
  }
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
