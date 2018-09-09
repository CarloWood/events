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
// the EVENT-SERVER. When that event occurs, the EVENT-GENERATOR passes the
// EVENT-DATA to the EVENT-SERVER, which passes it on to all EVENT-CLIENTs that
// requested the event.
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
// Then the EVENT-SERVER type of that EVENT-TYPE will be MyEventType::server_type.
// The static function MyEventType::server() will return a reference to the singleton
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

namespace event {

template<class TYPE>
class RequestBase
{
 public:
  using event_type_type = TYPE;

  // The event occured, pass `event_type' to the client that requested it,
  // by calling its call back function (or queuing it on the Busy Interface).
  virtual bool handle(TYPE const& event_type) = 0;
  // Called when the request was queued and the client becomes unbusy.
  virtual void rehandle(TYPE const& event_type) = 0;
};

template<class REQUESTBASE>
class RequestQueue
{
  using event_type_type = typename REQUESTBASE::event_type_type;

 protected:
  std::deqeue<std::function<void(event_type_type const&)>> m_queue;
};

template<class REQUESTBASE, class REQUESTQUEUE = RequestQueue<REQUESTBASE>>
class Server : public REQUESTQUEUE
{
  using event_type_type = typename REQUESTBASE::event_type_type;

 public:
  void operator()(std::function<void(event_type_type const&)> cb) { m_queue.push_back(cb); }
};

} // namespace event
