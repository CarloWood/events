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
 private:
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
  using EventData::EventData;
};

class FooEventRequestQueue
{
  using event_requests_type = std::list<event::Types<FooEventType>::request_ptr>;
  event_requests_type m_event_requests;
 protected:
  void add_request(event::Types<FooEventType>::request_ptr request) { m_event_requests.push_back(request); }
 public:
  void trigger(FooEventType const& data);
};

void FooEventRequestQueue::trigger(FooEventType const& data)
{
  for (auto&& event_request : m_event_requests)
    event_request->handle(data);
}

using FooEventServer = event::Server<FooEventType, FooEventRequestQueue>;

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

using BarEventServer = event::Server<BarEventType>;

//-----------------------------------------------------------------------------
//
// Two event clients
//

class MyEventClient1 : public event::BusyClient<2>      // 0 = foo, 1 = bar.
{
 public:
  void handle_foo(FooEventType const& data)
  {
    std::cout << "  MyEventClient1::foo(" << data << ")" << std::endl;
  }
  void handle_bar(BarEventType const& data)
  {
    std::cout << "  MyEventClient1::bar(" << data << ")" << std::endl;
  }
  ~MyEventClient1() { cancel_all_requests(); }
};

using Cookie = int;

class MyEventClient2 : public event::Client
{
 public:
  void handle_foo(FooEventType const& data, Cookie cookie)
  {
    std::cout << "  MyEventClient2::foo(" << data << ")" << std::endl;
    ASSERT(cookie == 123);
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
  using namespace std::placeholders;

  FooEventServer request_foo;
  BarEventServer request_bar;
  MyEventClient1 client1;
  MyEventClient2 client2;

  // Request events for Client1:
  request_foo(client1, std::bind(&MyEventClient1::handle_foo, client1, _1));
  request_bar(client1, std::bind(&MyEventClient1::handle_bar, client1, _1), 1);

  // Request event for Client2:
  Cookie cookie = 123;
  request_foo(client2, std::bind(&MyEventClient2::handle_foo, client2, _1, cookie));

  FooEventType footype(100);    // Event data of foo starts at 100.
  BarEventType bartype(200);    // Event data of bar starts at 200.

  cout << "Trigger foo(" << footype << ") -> client1, client2:" << endl;
  request_foo.trigger(footype);
  footype.inc();                // Increment event data of foo to 101.

  // Trigger events:
  cout << "client1 foo busy:" << endl;
  client1.set_busy();

  cout << "Trigger foo(" << footype << ") -> client1, client2:" << endl;
  request_foo.trigger(footype);
  footype.inc();                // Increment event data of foo to 102.

  cout << "Trigger bar(" << bartype << ") -> client1:" << endl;
  request_bar.trigger(bartype);
  bartype.inc();                // Increment event data of bar to 201.
  // Re-request bar, because that event resets every time.
  request_bar(client1, std::bind(&MyEventClient1::handle_bar, client1, _1), 1);

  cout << "client1 bar busy:" << endl;
  client1.set_busy(1);

  cout << "Trigger foo(" << footype << ") -> client1, client2:" << endl;
  request_foo.trigger(footype);
  footype.inc();

  cout << "Trigger bar(" << bartype << ") -> client1:" << endl;
  request_bar.trigger(bartype);
  bartype.inc();

  cout << "client1 foo unset busy:" << endl;
  client1.unset_busy();

  cout << "Trigger foo(" << footype << ") -> client1, client2:" << endl;
  request_foo.trigger(footype);
  footype.inc();

  cout << "Trigger bar(" << bartype << ") -> client1:" << endl;
  request_bar.trigger(bartype);
  bartype.inc();

  cout << "client1 bar unset busy:" << endl;
  client1.unset_busy(1);

  cout << "Leaving main" << endl;
}
