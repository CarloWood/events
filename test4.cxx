#include "sys.h"
#include "debug.h"
#include "Events.h"
#include <thread>
#include <chrono>

struct BalanceTimes
{
  using clock_t = std::chrono::high_resolution_clock;
  using time_point = std::chrono::time_point<clock_t>;
  time_point m_construction;
  time_point m_destruction;
  time_point::duration m_sleep;
};

struct BalanceSleep
{
  BalanceTimes& m_times;
  BalanceSleep(BalanceTimes& times) : m_times(times)
  {
    auto delta = m_times.m_destruction - m_times.m_construction;        // Time spent in the function last time (will be 0 the first time).
    m_times.m_construction = BalanceTimes::clock_t::now();
    if (delta.count() != 0)
      m_times.m_sleep += ((m_times.m_construction - m_times.m_destruction) - delta) / 10;
    // Sleep the same time as the time spent outside the function.
    if (m_times.m_sleep.count() > 0)
      std::this_thread::sleep_for(m_times.m_sleep);
    else
      m_times.m_sleep = BalanceTimes::time_point::duration();
  }
  ~BalanceSleep()
  {
    // Record time of destruction.
    m_times.m_destruction = BalanceTimes::clock_t::now();
  }
};

// Assume each event comes with certain values that need to
// be passed to the callback functions.
struct MyEventData
{
  int x;
  int y;
  MyEventData(int x, int y) : x(x), y(y) { }
};

// This class may be shared between multiple events, for example:
class MyEventType1 : public MyEventData
{
 public:
  static constexpr bool one_shot = false;
  using MyEventData::MyEventData;
#ifdef CWDEBUG
  friend std::ostream& operator<<(std::ostream& os, MyEventType1 const& data)
  {
    os << "{ " << data.x << ", " << data.y << '}';
    return os;
  }
#endif
};

class MyEventType2 : public MyEventData
{
 public:
  static constexpr bool one_shot = false;
  using MyEventData::MyEventData;
#ifdef CWDEBUG
  friend std::ostream& operator<<(std::ostream& os, MyEventType2 const& data)
  {
    os << "{ " << data.x << ", " << data.y << '}';
    return os;
  }
#endif
};

// Create an event server for each event type.
events::Server<MyEventType1> server1;
events::Server<MyEventType2> server2;

std::atomic_int count1 = ATOMIC_VAR_INIT(0);
std::atomic_int count2 = ATOMIC_VAR_INIT(0);

int constexpr loop_size = 100000;

// Whenever the events happen, call their respective trigger function.
void run1()
{
  Debug(NAMESPACE_DEBUG::init_thread());
  MyEventType1 my_event_data(1, 0);
  for (int i = 0; i < loop_size; ++i)
  {
    server1.trigger(my_event_data);
    my_event_data.y = ++count1;
    if (my_event_data.y > count2)
      std::this_thread::sleep_for(std::chrono::microseconds(1));
  }
}

void run2()
{
  Debug(NAMESPACE_DEBUG::init_thread());
  MyEventType2 my_event_data(2, 0);
  for (int i = 0; i < loop_size; ++i)
  {
    server2.trigger(my_event_data);
    my_event_data.y = ++count2;
    if (my_event_data.y > count1)
      std::this_thread::sleep_for(std::chrono::microseconds(1));
  }
}

// Assume you have an object MyClient that wants to receive events
// of both types, but only be called by one thread at a time.
class MyClient
{
  std::atomic_int m_inside;

  void callback1(MyEventType1 const& data);      // To be called when event 1 happens.
  void callback2(MyEventType2 const& data);      // To be called when event 2 happens.

  events::BusyInterface m_busy_interface;       // The busy interface for this object.
  events::BusyInterface m_busy_interface2;       // The busy interface for this object.
  events::RequestHandle<MyEventType1> m_handle1;
  events::RequestHandle<MyEventType2> m_handle2;

 public:
  MyClient() : m_inside(0) { }
  ~MyClient()
  {
    // Always call cancel() before destructing the handle, the busy interface, or
    // anything else that is needed for the callback function to be well-behaved.
    m_handle1.cancel();
    m_handle2.cancel();
  }

  void request();
};

// Request the events with the servers.
void MyClient::request()
{
  m_handle1 = server1.request(*this, &MyClient::callback1, m_busy_interface);
  m_handle2 = server2.request(*this, &MyClient::callback2, m_busy_interface);
}

int cb_count1;
int cb_count2;

void MyClient::callback1(MyEventType1 const& UNUSED_ARG(data))
{
  ASSERT(m_inside.fetch_add(1) == 0);
  static BalanceTimes times;
  BalanceSleep sleep(times);
  ++cb_count1;
  m_inside.fetch_sub(1);
}

void MyClient::callback2(MyEventType2 const& UNUSED_ARG(data))
{
  ASSERT(m_inside.fetch_add(1) == 0);
  static BalanceTimes times;
  BalanceSleep sleep(times);
  ++cb_count2;
  m_inside.fetch_sub(1);
}

int main()
{
  Debug(NAMESPACE_DEBUG::init());

  MyClient client;
  client.request();

  std::thread t1(run1);
  std::thread t2(run2);

  t1.join();
  t2.join();

  Dout(dc::notice, "cb_count1 = " << cb_count1 << "; cb_count2 = " << cb_count2);
  ASSERT(cb_count1 = loop_size && cb_count2 == loop_size);
}
