#include <iostream>
#include <functional>
#include <memory>

struct FooType {};
struct Cookie {};

using namespace std::placeholders;

class RequestBase
{
 protected:
  bool m_canceled;
 public:
  RequestBase() : m_canceled(false) { }
  virtual ~RequestBase() { }
  void cancel() { m_canceled = true; }
};

class Client
{
  std::vector<std::shared_ptr<RequestBase>> m_requests;
 protected:
  void cancel_all_requests()
  {
    for (auto& ptr : m_requests)
      ptr->cancel();
    m_requests.clear();
  }
 public:
  void add(std::shared_ptr<RequestBase> const& request_ptr)
  {
    m_requests.push_back(request_ptr);
  }
};

class Foo : public Client
{
 public:
  void foo(FooType const&, Cookie, int n)
  {
    std::cout << "Calling Foo::foo(" << n << ")\n";
  }

  ~Foo() { cancel_all_requests(); }
};

template<class TYPE>
class Request : public RequestBase
{
 private:
  std::function<void(TYPE const&)> m_callback;

 public:
  Request(std::function<void(TYPE const&)>&& callback) : m_callback(std::move(callback)) { std::cout << "Constructing Request [" << this << "]\n"; }
  ~Request() { std::cout << "Destructing Request [" << this << "]\n"; }
  void handle(TYPE const& type) { if (!m_canceled) m_callback(type); }
};

template<class TYPE>
class Server
{
  std::vector<std::shared_ptr<Request<TYPE>>> m_requests;

 public:
  template<class CLIENT, typename... Args>
  typename std::enable_if<std::is_base_of<Client, CLIENT>::value, void>::type
  request(CLIENT& client, void (CLIENT::*cb)(TYPE const&, Args...), Args... args)
  {
    std::cout << "Calling Server::request()\n";
    client.add(m_requests.emplace_back(new Request<TYPE>(std::bind(cb, &client, _1, args...))));
  }

  void trigger()
  {
    static TYPE type;
    for (auto& ptr : m_requests)
      ptr->handle(type);
    m_requests.clear();
  }
};

int main()
{
  Server<FooType> server;
  Foo foo;
  Cookie cookie;
  server.request(foo, &Foo::foo, cookie, 123);
  server.trigger();
}
