#include "sys.h"
#include "Events.h"

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

struct Foo : public event::Client
{
  void callback(MyEventType const& event) const
  {
    DoutEntering(dc::notice, "Foo::callback(" << event << ")");
  }

  void callback_with_cookie(MyEventType const& event, double cookie) const
  {
    DoutEntering(dc::notice, "Foo::callback_with_cookie(" << event << ", " << cookie << ")");
  }

  ~Foo() { cancel_all_requests(); }
};

class FakeClient : public event::Client
{
 public:
  ~FakeClient() { cancel_all_requests(); }
};

int main()
{
  Debug(NAMESPACE_DEBUG::init());
  using namespace std::placeholders;

  // Instantiate the event server for MyEventType (this should become a singleton).
  event::Server<MyEventType> event_server;

  // Register a callback by function pointer.
  FakeClient fake_client;
  event_server(fake_client, my_callback);

  // Register a member function of object foo as callback.
  Foo foo;
  foo.lock();
  event_server(foo, std::bind(&Foo::callback, &foo, _1));

  // Register a member function and pass a cookie.
  double const cookie = 3.1415;
  event_server(foo, std::bind(&Foo::callback_with_cookie, &foo, _1, cookie));

  // Pass a different cookie.
  event_server(foo, std::bind(&Foo::callback_with_cookie, &foo, _1, 0.999));

  // Use a lambda function as callback.
  event_server(fake_client, [cookie](MyEventType const& event){ Dout(dc::notice, "Calling lambda for event " << event << " and cookie " << cookie); });

  // Trigger the event.
  event_server.trigger(42);
}
