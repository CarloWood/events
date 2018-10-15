#include "sys.h"
#include "debug.h"
#include "Events.h"
#include <thread>

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

events::Server<FooType> server;

int constexpr N = 6;

void do_trigger()
{
  static std::atomic_int count;
  if (count == N - 2)
    return;
  ++count;
  Debug(NAMESPACE_DEBUG::init_thread());
  FooType type(21);
  server.trigger(type);
}

class Foo
{
  int m_magic;
  events::BusyInterface m_foo_bi;
  events::RequestHandle<FooType> m_handle;

 public:
  static std::thread s_trigger_threads[N];
  static std::atomic_int thr;

  void foo(FooType const& DEBUG_ONLY(type), Cookie, int DEBUG_ONLY(n))
  {
    DoutEntering(dc::notice, "Foo::foo(" << type << ", " << n << ")");
    ASSERT(m_magic == 12345678);
    std::thread t(do_trigger);
    int m = thr.fetch_add(1);
    ASSERT(m < N);
    t.swap(s_trigger_threads[m]);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  void request(void (Foo::*callback)(FooType const&, Cookie, int), Cookie cookie, int n)
  {
    m_handle = server.request(*this, callback, m_foo_bi, cookie, n);
  }

  Foo() : m_magic(12345678) { }
  ~Foo() { m_handle.cancel(); m_magic = 0; }
};

std::thread Foo::s_trigger_threads[N];
std::atomic_int Foo::thr;

int main()
{
  Debug(NAMESPACE_DEBUG::init());

  Cookie cookie;
  Foo foo;
  {
    // The request with cookie 222 is destructed before the event is triggered and should therefore never be called.
    events::RequestHandle<FooType> handle2 = server.request(foo, &Foo::foo, cookie, 222);
    foo.request(&Foo::foo, cookie, 111);
    handle2.cancel();
  }
  FooType type(42);
  server.trigger(type);
  server.trigger(type);

  for (int m = 0; m < N; ++m)
    if (Foo::s_trigger_threads[m].joinable())
      Foo::s_trigger_threads[m].join();
    else
      Dout(dc::warning, "s_trigger_threads[" << m << "] was not started.");
}
