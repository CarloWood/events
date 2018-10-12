#include "sys.h"
#include "debug.h"
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

using FooEventServer = events::Server<FooEventType>;

//-----------------------------------------------------------------------------
//
// BarEventServer: another EventServer
//

struct BarEventType : public EventData
{
  using EventData::EventData;
  static constexpr bool one_shot = true;
};

using BarEventServer = events::Server<BarEventType>;

//-----------------------------------------------------------------------------
//
// Two event clients
//

class MyEventClient1
{
 private:
  int m_magic;
  events::BusyInterface m_bi[2];        // 0 = foo, 1 = bar.

 public:
  void handle_foo(FooEventType const& data)
  {
    DoutEntering(dc::notice, "MyEventClient1::foo(" << data << ")");
    ASSERT(m_magic == 12345678);
  }
  void handle_bar(BarEventType const& data)
  {
    DoutEntering(dc::notice, "MyEventClient1::bar(" << data << ")");
  }
  MyEventClient1() : m_magic(12345678) { }
  ~MyEventClient1() { m_magic = 0; }

  void set_busy(int bi = 0) { m_bi[bi].set_busy(); }
  void unset_busy(int bi = 0) { m_bi[bi].unset_busy(); }

  events::BusyInterface& bi(int i) { return m_bi[i]; }
};

using Cookie = int;

class MyEventClient2
{
  int m_magic;
 public:
  void handle_foo(FooEventType const& data, Cookie cookie)
  {
    DoutEntering(dc::notice, "MyEventClient2::foo(" << data << ")");
    ASSERT(cookie == 123);
    ASSERT(m_magic == 123456789);
  }
  MyEventClient2() : m_magic(123456789) { }
  ~MyEventClient2() { m_magic = 0; }
};

//=============================================================================
//
// start
//
// Application initialisation.
//

int main()
{
  Debug(NAMESPACE_DEBUG::init());

  using namespace std::placeholders;

  FooEventServer request_foo;
  BarEventServer request_bar;

  FooEventType footype(100);    // Event data of foo starts at 100.
  BarEventType bartype(200);    // Event data of bar starts at 200.

  MyEventClient1 client1;
  events::RequestHandle<FooEventType> client1_foo_request;
  events::RequestHandle<BarEventType> client1_bar_request;
  {
    MyEventClient2 client2;
    events::RequestHandle<FooEventType> client2_foo_request;
    {
      MyEventClient2 client_tmp;
      client2 = std::move(client_tmp);
    }

    // Request events for Client1:
    client1_foo_request = request_foo.request(client1, &MyEventClient1::handle_foo, client1.bi(0));
    client1_bar_request = request_bar.request(client1, &MyEventClient1::handle_bar, client1.bi(1));

    // Request event for Client2:
    Cookie cookie = 123;
    client2_foo_request = request_foo.request(client2, &MyEventClient2::handle_foo, cookie);

    Dout(dc::notice, "Trigger foo(" << footype << ") -> client1, client2:");
    request_foo.trigger(footype);
    footype.inc();                // Increment event data of foo to 101.

    // Trigger events:
    Dout(dc::notice, "client1 foo busy:");
    client1.set_busy();

    Dout(dc::notice, "Trigger foo(" << footype << ") -> client1, client2:");
    request_foo.trigger(footype);
    footype.inc();                // Increment event data of foo to 102.

    Dout(dc::notice, "Trigger bar(" << bartype << ") -> client1:");
    request_bar.trigger(bartype);
    bartype.inc();                // Increment event data of bar to 201.
    // Re-request bar, because that event resets every time.
    client1_bar_request = request_bar.request(client1, &MyEventClient1::handle_bar, client1.bi(1));

    Dout(dc::notice, "client1 bar busy:");
    client1.set_busy(1);

    Dout(dc::notice, "Trigger foo(" << footype << ") -> client1, client2:");
    request_foo.trigger(footype);
    footype.inc();

    Dout(dc::notice, "Trigger bar(" << bartype << ") -> client1:");
    request_bar.trigger(bartype);
    bartype.inc();

    Dout(dc::notice, "client1 foo unset busy:");
    client1.unset_busy();

    client2_foo_request.cancel();
  } // Destruct client2.

  Dout(dc::notice, "Trigger foo(" << footype << ") -> client1, [client2]:");
  request_foo.trigger(footype);
  footype.inc();

  Dout(dc::notice, "Trigger bar(" << bartype << ") -> client1:");
  request_bar.trigger(bartype);
  bartype.inc();

  Dout(dc::notice, "client1 bar unset busy:");
  client1.unset_busy(1);

  client1_foo_request.cancel();
  client1_bar_request.cancel();

  Dout(dc::notice, "Leaving main");
}
