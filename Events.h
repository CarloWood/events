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

#pragma once

#include "debug.h"
#include "utils/NodeMemoryPool.h"
#include "utils/macros.h"
#include <functional>
#include <condition_variable>
#include <atomic>

namespace event {

class BusyInterface
{
};

template<typename TYPE>
class Server;

template<typename TYPE>
class Request
{
  static constexpr int s_cancel_marker = 0x10000;

 private:
  friend class Server<TYPE>;
  BusyInterface* m_busy_interface;
  std::function<void(TYPE const&)> m_callback;
  Request* m_next;
  std::atomic_int m_handle_count;
  std::mutex m_cancel_mutex;
  std::condition_variable m_cancel_cv;

 public:
  Request(std::function<void(TYPE const&)>&& callback, BusyInterface* busy_interface = nullptr) :
      m_busy_interface(busy_interface), m_callback(std::move(callback)), m_handle_count(0) { Dout(dc::notice, "Constructing Request [" << this << "]"); }
  ~Request()
  {
    Dout(dc::notice, "Destructing Request [" << this << "]");
    // Call reset() on the RequestHandle before destructing it.
    ASSERT(m_handle_count < 0);
  }

  bool start_handling();
  void handle(TYPE const& data);
  void stop_handling();
  void cancel();
};

template<typename TYPE>
class RequestHandle
{
 private:
  friend class Server<TYPE>;
  Request<TYPE>* m_request;

 public:
  RequestHandle();
  RequestHandle(Request<TYPE>* request);
  void reset();
};

template<typename TYPE>
class Server
{
 private:
  std::mutex m_request_list_mutex;              // Locked when changing any Request<TYPE>* that is part of m_request_list,
  Request<TYPE>* m_request_list;                // so that integrity of m_request_list is guaranteed when the mutex is not locked.
  utils::NodeMemoryPool m_request_memory_pool;

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
  Server() : m_request_list(nullptr), m_request_memory_pool(64, sizeof(Request<TYPE>)) { }

  void trigger(TYPE const& data);

  // Passing directly a std::function.
  [[nodiscard]] RequestHandle<TYPE> request(std::function<void(TYPE const&)>&& callback)
  {
    Dout(dc::notice, "Calling Server::request(std::function<void(" << libcwd::type_info_of<TYPE>().demangled_name() << " const&)>&&)");
    Request<TYPE>* request = new (m_request_memory_pool) Request<TYPE>(std::move(callback));
    push_front(request);
    return request;
  }

  // Passing directly a std::function and busy interface.
  [[nodiscard]] RequestHandle<TYPE> request(std::function<void(TYPE const&)>&& callback, BusyInterface& busy_interface)
  {
    Dout(dc::notice, "Calling Server::request(std::function<void(" << libcwd::type_info_of<TYPE>().demangled_name() << " const&)>&&, BusyInterface& [" << (void*)&busy_interface << "])");
    Request<TYPE>* request = new (m_request_memory_pool) Request<TYPE>(std::move(callback), &busy_interface);
    push_front(request);
    return request;
  }

  // Non-const client.
  template<class CLIENT, typename... Args>
  [[nodiscard]] RequestHandle<TYPE> request(CLIENT& client, void (CLIENT::*cb)(TYPE const&, Args...), Args... args)
  {
    Dout(dc::notice, "Calling Server::request(" << libcwd::type_info_of<CLIENT>().demangled_name() << "&, " << libcwd::type_info_of(cb).demangled_name() << ", ...)");
    using namespace std::placeholders;
    Request<TYPE>* request = new (m_request_memory_pool) Request<TYPE>(std::bind(cb, &client, _1, args...));
    push_front(request);
    return request;
  }

  template<class CLIENT, typename... Args>
  [[nodiscard]] RequestHandle<TYPE> request(CLIENT& client, void (CLIENT::*cb)(TYPE const&, Args...), BusyInterface& busy_interface, Args... args)
  {
    Dout(dc::notice, "Calling Server::request(" << libcwd::type_info_of<CLIENT>().demangled_name() << "&, " << libcwd::type_info_of(cb).demangled_name() << ", BusyInterface& [" << (void*)&busy_interface << "], ...)");
    using namespace std::placeholders;
    Request<TYPE>* request = new (m_request_memory_pool) Request<TYPE>(std::bind(cb, &client, _1, args...), &busy_interface);
    push_front(request);
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
    Dout(dc::notice, "Calling Server::request(" << libcwd::type_info_of<CLIENT>().demangled_name() << " const&, " << libcwd::type_info_of(cb).demangled_name() << ", ...)");
    using namespace std::placeholders;
    Request<TYPE>* request = new (m_request_memory_pool) Request<TYPE>(std::bind(cb, &client, _1, args...));
    push_front(request);
    return request;
  }

  template<class CLIENT, typename... Args>
  [[nodiscard]] RequestHandle<TYPE> request(CLIENT const& client, void (CLIENT::*cb)(TYPE const&, Args...) const, BusyInterface& busy_interface, Args... args)
  {
    Dout(dc::notice, "Calling Server::request(" << libcwd::type_info_of<CLIENT>().demangled_name() << " const&, " << libcwd::type_info_of(cb).demangled_name() << ", BusyInterface& [" << (void*)&busy_interface << "], ...)");
    using namespace std::placeholders;
    Request<TYPE>* request = new (m_request_memory_pool) Request<TYPE>(std::bind(cb, &client, _1, args...), &busy_interface);
    push_front(request);
    return request;
  }
};

template<typename TYPE>
RequestHandle<TYPE>::RequestHandle() : m_request(nullptr)
{
}

template<typename TYPE>
RequestHandle<TYPE>::RequestHandle(Request<TYPE>* request) : m_request(request)
{
}

template<typename TYPE>
void RequestHandle<TYPE>::reset()
{
  m_request->cancel();
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
    // The singly linked list has the following structure:
    // m_request_list --> Request<TYPE>
    //                    .m_next       --> Request<TYPE>
    //                                      .m_next       --> Request<TYPE>
    //                                                        .m_next == nullptr
    // Let *next point to the next Request<TYPE> object if it exists, otherwise *ptr == nullptr.
    Request<TYPE>** next = &m_request_list;     // Let *next point to the first object.
    for (;;)
    {
      Request<TYPE>*& request = *next;
      {
        std::lock_guard<std::mutex> lock(m_request_list_mutex);
        // Find and stop the next request object from being deleted (or delete it if that fails).
        while (request && !request->start_handling())
          delink(request);
      }
      if (!request)     // That was the last request in the list?
        break;

      request->handle(data);

      std::lock_guard<std::mutex> lock(m_request_list_mutex);
      request->stop_handling();               // Allow this request object to be canceled again.
      next = &request->m_next;
    }
  }
}

template<typename TYPE>
bool Request<TYPE>::start_handling()
{
  int handle_count = m_handle_count.fetch_add(1);
  bool result = handle_count >= 0;
  if (AI_UNLIKELY(!result))
  {
    // Minor optimization: don't call stop_handling() when we know that isn't necessary.
    // Note that this doesn't stop the possibility that stop_handling() will be called
    // and detect that m_handle_count reaches -0x10000 multiple times, it just makes it
    // very unlikely.
    if (handle_count == -s_cancel_marker)
      m_handle_count.fetch_sub(1);
    else
      stop_handling();
  }
  return result;
}

template<typename TYPE>
void Request<TYPE>::handle(TYPE const& data)
{
  m_callback(data);
}

template<typename TYPE>
void Request<TYPE>::stop_handling()
{
  if (m_handle_count.fetch_sub(1) == 1 - s_cancel_marker)
  {
    // Don't call notify_one() before the thread inside cancel() entered wait() and unlocked the mutex.
    m_cancel_mutex.lock();
    m_cancel_mutex.unlock();
    // Wake it up.
    m_cancel_cv.notify_one();
  }
}

template<typename TYPE>
void Request<TYPE>::cancel()
{
  // Lock m_cancel_mutex before (possibly) making the condition (m_handle_count reached -s_cancel_marker) true.
  std::unique_lock<std::mutex> lock(m_cancel_mutex);
  if (m_handle_count.fetch_sub(s_cancel_marker) == 0)   // Did we reach -s_cancel_marker?
    return;
  // Wait until m_handle_count becomes -s_cancel_marker.
  m_cancel_cv.wait(lock);
}

} // namespace event
