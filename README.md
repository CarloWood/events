# events submodule

This repository is a [git submodule](https://git-scm.com/book/en/v2/Git-Tools-Submodules)
providing a thread-safe event manager system that allows multiple threads
to generate events (also the same event) concurrently; dispatching the events
as callbacks, either one-shot or persisting (until `cancel()` is called on
the request handle, see below).

The following classes are defined (in namespace events):

* `Server<TYPE>` : an event server for event `TYPE`.
* `BusyInterface` : an object that optionally can be passed to `Server<TYPE>::request()`.
  Only one thread at a time will do a callback for all requests that pass the same BusyInterface;
  but without blocking threads on a mutex (non-blocking critical area).
* `RequestHandle<TYPE>` : the type returned by `Server<TYPE>::request()`.
  Keep this object around as long as you desire callbacks when events associated with `TYPE`
  happen. Call its method `cancel()` to stop callbacks and before destructing anything
  that needed for the callback to be valid (including the `RequestHandle` itself).

For example,

```
  // Declare an event server.
  Server<FooEvent> server;

  // One or more threads generate events:
  FooEvent data;
  server.trigger(data);

  // Some other thread.
  // Requesting callbacks for FooEvent.
  Foo foo;
  BusyInterface bi;
  auto handle = server.request(foo, &Foo::callback, bi);
  // Calls to Foo::callback(data) start happening, but only one thread at a time.

  // Cancel the request.
  handle.cancel();
  // Now it is safe to destruct foo, bi and handle (in any order).
```

The root project should be using
[autotools](https://en.wikipedia.org/wiki/GNU_Build_System_autotools) or
[cmake](https://cmake.org/),
[cwm4](https://github.com/CarloWood/cwm4) and
[cwds](https://github.com/CarloWood/cwds).

## Checking out a project that uses the events submodule.

To clone a project example-project that uses events simply run:

<pre>
<b>git clone --recursive</b> &lt;<i>URL-to-project</i>&gt;<b>/example-project.git</b>
<b>cd example-project</b>
<b>AUTOGEN_CMAKE_ONLY=1 ./autogen.sh</b>
</pre>

The ``--recursive`` is optional because ``./autogen.sh`` will fix
it when you forgot it.

When using [GNU autotools](https://en.wikipedia.org/wiki/GNU_Autotools) you should of course
not set ``AUTOGEN_CMAKE_ONLY``. Also, you probably want to use ``--enable-mainainer-mode``
as option to the generated ``configure`` script. ***WARNING: autotools are no longer tested (supported) by the author***

In order to use ``cmake`` configure as usual, for example to do a debug build with 16 cores:

    mkdir build_debug
    cmake -S . -B build_debug -DCMAKE_MESSAGE_LOG_LEVEL=DEBUG -DCMAKE_BUILD_TYPE=Debug -DCMAKE_VERBOSE_MAKEFILE=ON
    cmake --build build_debug --config Debug --parallel 16

Or to make a release build:

    mkdir build_release
    cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release
    cmake --build build_release --config Release --parallel 16

## Adding the events submodule to a project

To add this submodule to a project, that project should already
be set up to use [cwm4](https://github.com/CarloWood/cwm4).

Simply execute the following in a directory of that project
where you want to have the <tt>events</tt> subdirectory:

```
git submodule add https://github.com/CarloWood/events.git
```

This should clone events into the subdirectory <tt>events</tt>, or
if you already cloned it there, it should add it.

The instructions of adding any aicxx git submodule to a project
are virtually the same, so please have a look at the instructions
of [ai-utils](https://github.com/CarloWood/ai-utils/blob/master/README.md#adding-the-ai-utils-submodule-to-a-project)
for further details.
