#include "sys.h"
#include "debug.h"
#include "Events.h"

class Data
{
 private:
  int m_n;
 public:
  Data(int n) : m_n(n) { }
  friend std::ostream& operator<<(std::ostream& os, Data const& data)
  {
    return os << data.m_n;
  }
};

struct FooType : public Data
{
  using Data::Data;
  static bool constexpr one_shot = false;
};

struct Cookie { };

event::Server<FooType> server;

void do_trigger()
{
  static int count;
  if (++count == 4)
    return;
  Debug(NAMESPACE_DEBUG::init_thread());
  FooType type(21);
  server.trigger(type);
}

class Foo
{
  int m_magic;
  event::BusyInterface m_foo_bi;
  event::request_handle<FooType> m_handle;

 public:
  static std::thread s_trigger_thread;

  void foo(FooType const& type, Cookie, int n)
  {
    DoutEntering(dc::notice, "Foo::foo(" << type << ", " << n << ")");
    ASSERT(m_magic == 12345678);
    std::thread t(do_trigger);
    t.swap(s_trigger_thread);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  void request(void (Foo::*callback)(FooType const&, Cookie, int), Cookie cookie, int n)
  {
    m_handle = server.request(*this, callback, m_foo_bi, cookie, n);
  }

  Foo() : m_magic(12345678) { }
  ~Foo() { m_handle.reset(); m_magic = 0; }
};

std::thread Foo::s_trigger_thread;

int main()
{
  Debug(NAMESPACE_DEBUG::init());

  Cookie cookie;
  Foo foo;
  {
    // The request with cookie 222 is destructed before the event is triggered and should therefore never be called.
    event::request_handle<FooType> handle2 = server.request(foo, &Foo::foo, cookie, 222);
    foo.request(&Foo::foo, cookie, 111);
  }
  FooType type(42);
  server.trigger(type);
  server.trigger(type);

  Foo::s_trigger_thread.join();
}
