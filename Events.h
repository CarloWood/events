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

#pragma once

#include "debug.h"
#include "utils/AIRefCount.h"
#include <deque>
#include <functional>
#include <array>

namespace event {

// Forward declarations.
class Client;

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

//=================================================================================
//
// ClientTracker
//

class ClientTracker
{
 private:
  Client* m_client;
  int m_ref_count;

 public:
  ClientTracker(Client* client) : m_client(client), m_ref_count(1) { }

  void cancel_all_requests() { m_client = nullptr; }

  // Accessor.
  bool all_requests_canceled() const { return !m_client; }

  void copied() { ++m_ref_count; }
  void release() { if (--m_ref_count == 0) delete this; }

  void set_client(Client* client) { m_client = client; }
};

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
  inline void queue(typename Types<TYPE>::request_ptr request, TYPE const& type);

 private:
  template<class TYPE>
  class QueuedEvent final : public QueuedEventBase
  {
   private:
    typename Types<TYPE>::request_ptr m_request;
    TYPE const m_type;
   private:
    friend void BusyInterface::queue<TYPE>(typename Types<TYPE>::request_ptr, TYPE const&);
    QueuedEvent(typename Types<TYPE>::request_ptr request, TYPE const& type) : m_request(request), m_type(type) { }
   private:
    void retrigger() override { m_request->rehandle(m_type); delete this; }
  };

 private:
  unsigned int m_busy_depth;                    // Busy depth counter.  The client is busy when this is larger than zero.
  std::list<QueuedEventBase*> m_events;         // List with pointers to queued events that could not be handled at the moment the event
                                                // occurred because the client was busy.

 public:
  // Constructor
  BusyInterface() : m_busy_depth(0) { }

  // Accessors
  bool is_busy() const { return m_busy_depth > 0; }

  // Manipulators

  // Increment busy depth counter.
  void set_busy() { ++m_busy_depth; }

  // Decrement busy depth counter and flush all queued events, if any, when the interface becomes not busy.
  void unset_busy();
};

template<class TYPE>
class Request : public AIRefCount
{
 private:
  ClientTracker* m_client_tracker;
  typename Types<TYPE>::callback m_callback;
  BusyInterface* m_busy_interface;

 public:
  // The event occured, pass `type' to the client that requested it,
  // by calling its call back function (or queuing it on the Busy Interface).
  bool handle(TYPE const& type);

  // Called when the request was queued and the client becomes unbusy.
  void rehandle(TYPE const& type);

 public:
  // Constructed by the event Server when a Client requests to be notified about its event.
  Request(ClientTracker* client_tracker, typename Types<TYPE>::callback callback) :
      m_client_tracker(client_tracker), m_callback(callback), m_busy_interface(nullptr) { client_tracker->copied(); }

  // Constructed by the event Server when a Client requests to be notified about its event.
  Request(ClientTracker* client_tracker, typename Types<TYPE>::callback callback, BusyInterface& busy_interface) :
      m_client_tracker(client_tracker), m_callback(callback), m_busy_interface(&busy_interface) { client_tracker->copied(); }

  // You shouldn't use this.
  Request(Request const&) = delete;

  bool canceled() const { return m_client_tracker->all_requests_canceled(); }

 protected:
  // Destructor.
  virtual ~Request() { m_client_tracker->release(); }
};

template<class TYPE>
bool Request<TYPE>::handle(TYPE const& type)
{
  if (m_client_tracker->all_requests_canceled())
    return true;
  if (!m_busy_interface)
    m_callback(type);
  else if (m_busy_interface->is_busy())
    m_busy_interface->queue(this, type);
  else
  {
    m_busy_interface->set_busy();
    m_callback(type);
    m_busy_interface->unset_busy();
  }
  return false;
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

//-----------------------------------------------------------------------------
//
// Implementation of the inline methods of busy_interface_ct.
//

inline void BusyInterface::unset_busy()
{
  if (m_busy_depth == 1 && !m_events.empty())
    flush_events();
#ifdef CWDEBUG
  if (m_busy_depth == 0)
    DoutFatal(dc::core, "Calling unset_busy() more often than set_busy()");
#endif
  --m_busy_depth;
}

template<class TYPE>
inline void BusyInterface::queue(typename Types<TYPE>::request_ptr request, TYPE const& type)
{
  QueuedEvent<TYPE>* queued_event = new QueuedEvent<TYPE>(request, type);
  AllocTag1(queued_event);
  m_events.push_back(queued_event);
}

//=================================================================================
//
// Client
//

class Client
{
 private:
  ClientTracker* m_client_tracker;
  Client* m_the_real_me;

 public:
  Client() : m_the_real_me(nullptr) { m_client_tracker = NEW(ClientTracker(this)); }
  Client(Client const& client) : m_client_tracker(client.m_client_tracker), m_the_real_me(nullptr) { client.m_client_tracker->copied(); }
#ifdef CWDEBUG
  ~Client()
  {
    if (m_client_tracker)
      DoutFatal(dc::core, "You should call 'cancel_all_requests()' from the destructor of the most-derived object.");
  }
#endif
  // Call this when the event client is destructed (from the most derived object).
  void cancel_all_requests()
  {
    if (m_client_tracker)
    {
      if (m_the_real_me == this)
        m_client_tracker->cancel_all_requests();
      m_client_tracker->release();
      m_client_tracker = nullptr;
    }
  }

  void lock()
  {
    ASSERT(!m_the_real_me || m_the_real_me == this);
    m_the_real_me = this;
    m_client_tracker->set_client(this);
  }

  ClientTracker* client_tracker() const { return m_client_tracker; }
};

template<int number_of_busy_interfaces = 1>
class BusyClient : public Client
{
  using Client::Client;

 private:
  std::array<BusyInterface, number_of_busy_interfaces> m_busy_interfaces;

 public:
  BusyInterface& busy_interface(int n) { return m_busy_interfaces[n]; }

  void set_busy(int n = 0) { m_busy_interfaces[n].set_busy(); }
  void unset_busy(int n = 0) { m_busy_interfaces[n].unset_busy(); }
};

//=================================================================================
//
// Server
//

template<class TYPE>
class Server
{
  using request_ptr = typename Types<TYPE>::request_ptr;
  using requests_type = std::vector<request_ptr>;

 private:
  requests_type m_requests;

 protected:
  void add_request(request_ptr request) { m_requests.push_back(request); }

 public:
  // Add callback request.
  void operator()(Client const& client, typename Types<TYPE>::callback callback)
  {
    this->add_request(NEW(Request<TYPE>(client.client_tracker(), callback)));
  }

  template<int number_of_busy_interfaces>
  void operator()(BusyClient<number_of_busy_interfaces>& client, typename Types<TYPE>::callback callback, int n = 0)
  {
    ASSERT(0 <= n && n < number_of_busy_interfaces);
    this->add_request(NEW(Request<TYPE>(client.client_tracker(), callback, client.busy_interface(n))));
  }

  void trigger(TYPE const& type, bool clear_requests = true);
};

//-----------------------------------------------------------------------------
//
// Implementation of methods of Server.
//

template<class TYPE>
void Server<TYPE>::trigger(TYPE const& type, bool clear_requests)
{
  for (auto&& request : m_requests)
    request->handle(type);
  if (clear_requests)
    m_requests.clear();
}

} // namespace event
