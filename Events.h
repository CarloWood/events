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
// Example
// -------
//
// Assume there is defined a class MyEventType.
//
// This class will be derived from a class MyEventData, which is conviently
// accessible as MyEventType::data_type;
//
//     I.e.
//
//     class MyEventData;                               // EVENT-DATA
//     class MyEventType : public MyEventData {         // EVENT-TYPE
//      public:
//       using data_type = MyEventData;
//       ...
//
// The EVENT-SERVER type of that EVENT-TYPE should be MyEventType::server_type.
// The static function MyEventType::server() must return a reference to the singleton
// event server that handles this EVENT-TYPE.
//
//       ...
//       using request_base_type = MyEventRequestBase;
//       using request_queue_type = MyEventRequestQueue;
//       using server_type = EventServer<request_base_type, request_queue_type>;
//       server_type& server();
//     };
//
// The EVENT-SERVER depends on two user defined classes (here called MyEventRequestBase
// and MyEventRequestQueue).
//
//
// At some place in the code (the event generator) an 'event' happens:
//
//     if (3 < x && x > 7 && 9 < y && y < 20)
//     {
//       rectangle_events.handle_event(EVENT_37920, x, y);
//       ^^^^^^^^^^^^^^^^ ^^^^^^^^^^^^ ^^^^^^^^^^^^^^^^^
//         \_event server  \_event trigger   \_event data.
//
// where the type of rectangle_events is a class derived from 

#pragma once

#include "debug.h"
#include "utils/AIRefCount.h"
#include <deque>
#include <functional>

namespace event {

class BusyInterface
{
 private:
  void flush_events();          // Flush all queued events until the list is empty or the client is marked busy again.
                                // The latter can happen when one of the call back functions calls `is_busy()' for this interface.

  class QueuedEventBase
  {
    virtual ~QueuedEventBase() { }
    friend void BusyInterface::flush_events();
    virtual void retrigger() = 0;
  };

 public:
  // Queue the event "request / type".
  // This event will be handled as soon as `unset_busy' is called while m_busy_depth is 1.
  template<class REQUESTBASE>
  inline void queue(boost::intrusive_ptr<REQUESTBASE> request, typename REQUESTBASE::event_type_type const& type);

 private:
  template<class REQUESTBASE>
  class QueuedEvent : public QueuedEventBase
  {
   private:
    boost::intrusive_ptr<REQUESTBASE> m_request;
    typename REQUESTBASE::event_type_type const& m_type;
   private:
    friend void BusyInterface::queue<REQUESTBASE>(boost::intrusive_ptr<REQUESTBASE>, typename REQUESTBASE::event_type_type const&);
    QueuedEvent(boost::intrusive_ptr<REQUESTBASE> request, typename REQUESTBASE::event_type_type const& type) : m_request(request), m_type(type) { }
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

  //
  // BusyInterface::None
  //
  // A special Busy Interface which is used for Event Requests for which the
  // Event Client is never busy. Compiler optimization should completely get
  // rid of the busy interface code when this is used.
  //
  class None {
   public:
    bool is_busy() const { return false; }
    void set_busy() const { }
    void unset_busy() const { }
    template<class REQUESTBASE>
    void queue(boost::intrusive_ptr<REQUESTBASE> UNUSED_ARG(request), typename REQUESTBASE::event_type_type const& UNUSED_ARG(type)) const { }
  };

  static constexpr None none{};
};

//-----------------------------------------------------------------------------
//
// Implementation of the inline methods of busy_interface_ct.
//

inline void BusyInterface::unset_busy()
{
  if (m_busy_depth == 1&& !m_events.empty())
    flush_events();
#ifdef CWDEBUG
  if (m_busy_depth == 0)
    DoutFatal(dc::core, "Calling unset_busy() more often than set_busy()");
#endif
  --m_busy_depth;
}

template<class REQUESTBASE>
inline void BusyInterface::queue(boost::intrusive_ptr<REQUESTBASE> request, typename REQUESTBASE::event_type_type const& type)
{
  QueuedEvent<REQUESTBASE>* queued_event = new QueuedEvent<REQUESTBASE>(request, type);
  AllocTag1(queued_event);
  m_events.push_back(queued_event);
}

//=================================================================================
//
// ClientTracker
//

class Client;
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

template<class TYPE>
class RequestBase : public AIRefCount
{
 public:
  using event_type_type = TYPE;

 protected:
  ClientTracker* m_client_tracker;

 public:
  // The event occured, pass `type' to the client that requested it,
  // by calling its call back function (or queuing it on the Busy Interface).
  virtual bool handle(TYPE const& type) = 0;

  // Called when the request was queued and the client becomes unbusy.
  virtual void rehandle(TYPE const& type) = 0;

 public:
  // Constructed by the event Server when a Client requests to be notified about its event.
  RequestBase(ClientTracker* client_tracker) : m_client_tracker(client_tracker) { client_tracker->copied(); }

  // You shouldn't use this.
  RequestBase(RequestBase const&) = delete;

  bool canceled() const { return m_client_tracker->all_requests_canceled(); }

 protected:
  // Destructor.
  virtual ~RequestBase() { m_client_tracker->release(); }
};

//=================================================================================
//
// RequestQueue
//
// The default REQUESTQUEUE
//
// You never need this class - just define your own when this doesn't suffice.
//

template<class REQUESTBASE>
class RequestQueue
{
  using event_type_type = typename REQUESTBASE::event_type_type;
  using requests_type = std::deque<boost::intrusive_ptr<REQUESTBASE>>

 private:
  requests_type m_requests;

 protected:
  void add_request(boost::intrusive_ptr<REQUESTBASE> request) { m_requests.push_back(request); }

 public:
  void trigger(event_type_type const& type);

#if 0
  // Add callback request.
  void operator()(std::function<void(event_type_type const&)> callback) { m_queue.push_back(callback); }

  template<typename... Args>
  void trigger(Args&&... args) { trigger(static_cast<event_type_type const&>(event_type_type(std::forward<Args>(args)...))); }

  void trigger(event_type_type const& type);
#endif
};

//-----------------------------------------------------------------------------
//
// Implementation of methods of RequestQueue.
//

template<class REQUESTBASE>
void RequestQueue<REQUESTBASE>::trigger(event_type_type const& type)
{
  for (auto&& request : m_requests)
  {
    request->handle(type);
  }
  m_requests.clear();
}

//=================================================================================
//
// Server
//

template<class REQUESTBASE, class REQUESTQUEUE = RequestQueue<REQUESTBASE>>
class Server : public REQUESTQUEUE
{
  using event_type_type = typename REQUESTBASE::event_type_type;
};

} // namespace event
