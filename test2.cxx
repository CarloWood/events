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

struct FooEventType : public EventData
{
  using EventData::EventData;
  static constexpr bool one_shot = false;
};

using FooEventServer = event::Server<FooEventType>;

//-----------------------------------------------------------------------------
//
// BarEventServer: another EventServer
//

struct BarEventType : public EventData
{
  using EventData::EventData;
  static constexpr bool one_shot = true;
};

using BarEventServer = event::Server<BarEventType>;

//-----------------------------------------------------------------------------
//
// Two event clients
//

class MyEventClient1 : public event::BusyClient<2>      // 0 = foo, 1 = bar.
{
  int m_magic;
 public:
  void handle_foo(FooEventType const& data)
  {
    std::cout << "  MyEventClient1::foo(" << data << ")" << std::endl;
    ASSERT(m_magic == 12345678);
  }
  void handle_bar(BarEventType const& data)
  {
    std::cout << "  MyEventClient1::bar(" << data << ")" << std::endl;
  }
  MyEventClient1() : m_magic(12345678) { }
  ~MyEventClient1() { cancel_all_requests(); m_magic = 0; }
};

using Cookie = int;

class MyEventClient2 : public event::Client
{
  int m_magic;
 public:
  void handle_foo(FooEventType const& data, Cookie cookie)
  {
    std::cout << "  MyEventClient2::foo(" << data << ")" << std::endl;
    ASSERT(cookie == 123);
    ASSERT(m_magic == 123456789);
  }
  void operator=(MyEventClient2&& client) { event::Client::operator=(std::move(client)); }
  MyEventClient2() : m_magic(123456789) { }
  ~MyEventClient2() { cancel_all_requests(); m_magic = 0; }
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

  FooEventType footype(100);    // Event data of foo starts at 100.
  BarEventType bartype(200);    // Event data of bar starts at 200.

  MyEventClient1 client1;
  {
    MyEventClient2 client2;
    {
      MyEventClient2 client_tmp;
      client2 = std::move(client_tmp);
    }

    client1.lock();
    //client2.lock();

    // Request events for Client1:
    request_foo(client1, std::bind(&MyEventClient1::handle_foo, &client1, _1));
    request_bar(client1, std::bind(&MyEventClient1::handle_bar, &client1, _1), 1);

    // Request event for Client2:
    Cookie cookie = 123;
    request_foo(client2, std::bind(&MyEventClient2::handle_foo, &client2, _1, cookie));

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
    request_bar(client1, std::bind(&MyEventClient1::handle_bar, &client1, _1), 1);

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

  } // Destruct client2.

  cout << "Trigger foo(" << footype << ") -> client1, [client2]:" << endl;
  request_foo.trigger(footype);
  footype.inc();

  cout << "Trigger bar(" << bartype << ") -> client1:" << endl;
  request_bar.trigger(bartype);
  bartype.inc();

  cout << "client1 bar unset busy:" << endl;
  client1.unset_busy(1);

  cout << "Leaving main" << endl;
}
