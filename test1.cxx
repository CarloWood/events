#include "sys.h"
#include "Events.h"
#include <functional>

class MyEventData
{
 protected:
  int m_n;

 public:
  MyEventData(int n) : m_n(n) { }

  friend std::ostream& operator<<(std::ostream& os, MyEventData const& data);
};

std::ostream& operator<<(std::ostream& os, MyEventData const& data)
{
  os << "MyEventData:{" << data.m_n << "}";
  return os;
}

struct MyEventType : public MyEventData
{
  using MyEventData::MyEventData;
  static constexpr bool one_shot = true;
};

void my_callback(MyEventType const& event)
{
  DoutEntering(dc::notice, "my_callback(" << event << ")");
}

struct Foo
{
  void callback(MyEventType const& event) const
  {
    DoutEntering(dc::notice, "Foo::callback(" << event << ")");
  }

  void callback_with_cookie(MyEventType const& event, double cookie) const
  {
    DoutEntering(dc::notice, "Foo::callback_with_cookie(" << event << ", " << cookie << ")");
  }
};

int main()
{
  Debug(NAMESPACE_DEBUG::init());
  using namespace std::placeholders;

  // Instantiate the event server for MyEventType (this should become a singleton).
  event::Server<MyEventType> event_server;

  // Register a callback by function pointer.
  auto handle1 = event_server.request(my_callback);

  // Register a member function of object foo as callback.
  Foo foo;
  event::RequestHandle<MyEventType> foo_request[3];
  foo_request[0] = event_server.request(foo, &Foo::callback);

  // Register a member function and pass a cookie.
  double const cookie = 3.1415;
  foo_request[1] = event_server.request(foo, &Foo::callback_with_cookie, cookie);

  // Pass a different cookie.
  foo_request[2] = event_server.request(foo, &Foo::callback_with_cookie, 0.999);

  // Use a lambda function as callback.
  auto handle2 = event_server.request([cookie](MyEventType const& event){ Dout(dc::notice, "Calling lambda for event " << event << " and cookie " << cookie); });

  // Trigger the event.
  event_server.trigger(42);

  for (int i = 0; i < 3; ++i)
    foo_request[i].cancel();
  handle1.cancel();
  handle2.cancel();
}
