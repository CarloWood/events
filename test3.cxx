#include "sys.h"
#include <iostream>
#include <functional>
#include <memory>
#include <list>
#include <vector>
#include <cassert>
#include "utils/AIRefCount.h"
#include "utils/NodeMemoryPool.h"

namespace event {

class BusyInterface;

template<class TYPE>
class Request : public AIRefCount
{
 private:
  BusyInterface* m_busy_interface;
  std::function<void(TYPE const&)> m_callback;

 public:
  Request(std::function<void(TYPE const&)>&& callback, BusyInterface* busy_interface = nullptr) :
      m_busy_interface(busy_interface), m_callback(std::move(callback)) { std::cout << "Constructing Request [" << this << "]\n"; }
  ~Request() { std::cout << "Destructing Request [" << this << "]\n"; }
  bool handle(TYPE const& type);
  void rehandle(TYPE const& type);
  void operator delete(void* ptr) { utils::NodeMemoryPool::static_free(ptr); }
};

class BusyInterface
{
 private:
  void flush_events();          // Flush all queued events until the list is empty or the client is marked busy again.
                                // The latter can happen when one of the call back functions calls `is_busy()' for this interface.

  class QueuedEventBase
  {
    friend void BusyInterface::flush_events();
    virtual void retrigger() = 0;
  };

 public:
  // Queue the event "request / type".
  // This event will be handled as soon as `unset_busy' is called while m_busy_depth is 1.
  template<class TYPE>
  inline void queue(Request<TYPE>* request, TYPE const& type);

 private:
  template<class TYPE>
  class QueuedEvent final : public QueuedEventBase
  {
   private:
    boost::intrusive_ptr<Request<TYPE>> m_request;
    TYPE const m_type;
   private:
    friend void BusyInterface::queue<TYPE>(Request<TYPE>*, TYPE const&);
    QueuedEvent(Request<TYPE>* request, TYPE const& type) : m_request(request), m_type(type) { }
   private:
    void retrigger() override { m_request->rehandle(m_type); delete this; }
  };

 private:
  unsigned int m_busy_depth;                    // Busy depth counter.  The client is busy when this is larger than zero.
  std::list<QueuedEventBase*> m_events;         // List with pointers to queued events that could not be handled at the moment the event
                                                // occurred because the client was busy.

 public:
  // Constructor
  BusyInterface() : m_busy_depth(0) { }

  // Accessors
  bool is_busy() const { return m_busy_depth > 0; }

  // Manipulators

  // Increment busy depth counter.
  void set_busy() { ++m_busy_depth; }

  // Decrement busy depth counter and flush all queued events, if any, when the interface becomes not busy.
  void unset_busy();
};

inline void BusyInterface::unset_busy()
{
  if (m_busy_depth == 1 && !m_events.empty())
    flush_events();
#ifdef CWDEBUG
  if (m_busy_depth == 0)
    DoutFatal(dc::core, "Calling unset_busy() more often than set_busy()");
#endif
  --m_busy_depth;
}

template<class TYPE>
void BusyInterface::queue(Request<TYPE>* request, TYPE const& type)
{
  QueuedEvent<TYPE>* queued_event = new QueuedEvent<TYPE>(request, type);
  AllocTag1(queued_event);
  m_events.push_back(queued_event);
}

template<class TYPE>
bool Request<TYPE>::handle(TYPE const& type)
{
  std::cout << "ref count = " << ref_count() << std::endl;
  if (unique())
    return true;
  if (!m_busy_interface)
    m_callback(type);
  else if (m_busy_interface->is_busy())
    m_busy_interface->queue(this, type);
  else
  {
    m_busy_interface->set_busy();
    m_callback(type);
    m_busy_interface->unset_busy();
  }
  return false;
}

template<class TYPE>
void Request<TYPE>::rehandle(TYPE const& type)
{
#ifndef CWDEBUG
  if (!m_busy_interface || !m_busy_interface.is_busy())
    DoutFatal(dc::core, "Calling event::Request<TYPE>::rehandle() without busy BusyInterface.");
#endif
  m_callback(type);
}

template<class TYPE>
class Server
{
  std::vector<boost::intrusive_ptr<Request<TYPE>>> m_requests;
  utils::NodeMemoryPool m_request_pool;

 public:
  Server() : m_request_pool(64, sizeof(Request<TYPE>)) { }

  template<class CLIENT, typename... Args>
  boost::intrusive_ptr<Request<TYPE>>& request(CLIENT& client, void (CLIENT::*cb)(TYPE const&, Args...), Args... args)
  {
    std::cout << "Calling Server::request()\n";
    using namespace std::placeholders;
    return m_requests.emplace_back(new (m_request_pool) Request<TYPE>(std::bind(cb, &client, _1, args...)));
  }

  void trigger(TYPE const& type);
};

template<class TYPE>
void Server<TYPE>::trigger(TYPE const& type)
{
  if (TYPE::one_shot)
  {
    for (auto& request : m_requests)
      request->handle(type);
    m_requests.clear();
  }
  else
    m_requests.erase(
        std::remove_if(m_requests.begin(),
                       m_requests.end(),
                       [&type](boost::intrusive_ptr<Request<TYPE>> const& request)
                       { return request->handle(type); }
                      ),
        m_requests.end());
}

// .cxx
void BusyInterface::flush_events()
{
#ifdef CWDEBUG
  if (m_busy_depth != 1)
    DoutFatal(dc::core, "Expected `m_busy_depth' to be 1 in flush_events()");
#endif
  do
  {
    m_events.front()->retrigger();
    m_events.pop_front();
  }
  while (m_busy_depth == 1 && !m_events.empty());
}

} // namespace event

struct FooType { static bool constexpr one_shot = false; };
struct Cookie {};

class Foo
{
  int m_magic;

 public:
  void foo(FooType const&, Cookie, int n)
  {
    std::cout << "Calling Foo::foo(" << n << ")\n";
    assert(m_magic == 12345678);
  }

  Foo() : m_magic(12345678) { }
  ~Foo() { m_magic = 0; }
};

int main()
{
  event::Server<FooType> server;
  Cookie cookie;
  Foo foo;
  boost::intrusive_ptr<event::Request<FooType>> handle1;
  {
    boost::intrusive_ptr<event::Request<FooType>> handle2 = server.request(foo, &Foo::foo, cookie, 222);
    handle1 = server.request(foo, &Foo::foo, cookie, 111);
  }
  FooType type;
  server.trigger(type);
  server.trigger(type);
}
