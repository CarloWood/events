#include "sys.h"
#include "Events.h"
#include <iostream>
#include <list>

//=============================================================================
//
// Start of test classes
//

class EventData
{
  int m_i;
 public:
  EventData(int i) : m_i(i) { }
  void inc() { ++m_i; }
  friend std::ostream& operator<<(std::ostream& os, EventData const& data) { return os << data.m_i; }
};

//-----------------------------------------------------------------------------
//
// FooEventServer: an EventServer
//

class FooEventType : public EventData
{
 public:
  using event_request_data_type = void;
  FooEventType(int i) : EventData(i) { }
  bool check_range() const { return true; }
};

class FooEventRequestQueue
{
  using event_requests_type = std::list<boost::intrusive_ptr<event::RequestBase<FooEventType>>>;
  event_requests_type m_event_requests;
 protected:
  void add_request(boost::intrusive_ptr<event::RequestBase<FooEventType>> request) { m_event_requests.push_back(request); }
 public:
  void trigger(FooEventType const& data);
};

void FooEventRequestQueue::trigger(FooEventType const& data)
{
  for (auto&& event_request : m_event_requests)
    event_request->handle(data);
}

using FooEventServer = event::Server<event::RequestBase<FooEventType>, FooEventRequestQueue>;

//-----------------------------------------------------------------------------
//
// BarEventServer: another EventServer
//

class BarEventType : public EventData
{
 public:
  using event_request_data_type = void;
  BarEventType(int i) : EventData(i) { }
  bool check_range() const { return true; }
};

using BarEventServer = event::Server<event::RequestBase<BarEventType>>;

//-----------------------------------------------------------------------------
//
// Two event clients
//

class MyEventClient1 : public event::Client
{
 public:
  event::BusyInterface bi_foo, bi_bar;
  void handle_foo(FooEventType const& data)
  {
    std::cout << "foo(" << data << ") -> 1" << std::endl;
  }
  void handle_bar(BarEventType const& data)
  {
    std::cout << "bar(" << data << ") -> 1" << std::endl;
  }
  ~MyEventClient1() { cancel_all_requests(); }
};

using Cookie = int;

class MyEventClient2 : public event::Client
{
 public:
  void handle_foo(FooEventType const& data, Cookie cookie)
  {
    std::cout << "foo(" << data << ") -> 2, cookie = " << cookie << std::endl;
  }
  ~MyEventClient2() { cancel_all_requests(); }
};

//=============================================================================
//
// start
//
// Application initialisation.
//

int main()
{
  using std::cout;
  using std::endl;

  FooEventServer request_foo;
  BarEventServer request_bar;
  MyEventClient1 x;
  MyEventClient2 y;

  // Request events for Client1:
  request_foo(x, &MyEventClient1::handle_foo, x.bi_foo);
  request_bar(x, &MyEventClient1::handle_bar, x.bi_bar);

  // Request event for Client2:
  Cookie cookie = 123;
  request_foo(y, &MyEventClient2::handle_foo, cookie);

  FooEventType footype(1);
  BarEventType bartype(101);
  // Trigger events:
  cout << "bi_foo busy:" << endl;
  x.bi_foo.set_busy();
  cout << "Trigger foo -> 1, 2:" << endl;
  request_foo.trigger(footype); footype.inc();
  cout << "Trigger bar -> 1:" << endl;
  request_bar.trigger(bartype); bartype.inc();
  cout << "bi_bar busy:" << endl;
  x.bi_bar.set_busy();
  cout << "Trigger foo -> 1, 2:" << endl;
  request_foo.trigger(footype); footype.inc();
  cout << "Trigger bar -> 1:" << endl;
  request_bar.trigger(bartype); bartype.inc();
  cout << "bi_foo not busy:" << endl;
  x.bi_foo.unset_busy();
  cout << "Trigger foo -> 1, 2:" << endl;
  request_foo.trigger(footype); footype.inc();
  cout << "Trigger bar -> 1:" << endl;
  request_bar.trigger(bartype); bartype.inc();
  cout << "bi_bar not busy:" << endl;
  x.bi_bar.unset_busy();
}
