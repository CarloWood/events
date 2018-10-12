# events submodule

This repository is a [git submodule](https://git-scm.com/book/en/v2/Git-Tools-Submodules)
providing a thread-safe event manager system that allows multiple threads
to generate events (also the same event) concurrently; dispatching the events
as callbacks, either one-shot or persisting (until cancel() is called).

The following classes are defined (in namespace events):

* `Server<TYPE>` : an event server for event `TYPE`.
* `BusyInterface` : an object that optionally can be passed to `Server<TYPE>::request()`.
  Only one thread at a time will do a callback for all requests that pass the same BusyInterface;
  but without blocking threads on a mutex (non-blocking critical area).
* `RequestHandle<TYPE>` : the type returned by `Server<TYPE>::request()`.
  Keep this object around as long as you desire callbacks when events associated with `TYPE`
  happen. Call its method `cancel()` to stop callbacks and before destructing anything
  that needed for the callback to be valid (including the RequestHandle itself).

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
[autotools](https://en.wikipedia.org/wiki/GNU_Build_System_autotools),
[cwm4](https://github.com/CarloWood/cwm4) and
[libcwd](https://github.com/CarloWood/libcwd).

## Checking out a project that uses the events submodule.

To clone a project example-project that uses events simply run:

<pre>
<b>git clone --recursive</b> &lt;<i>URL-to-project</i>&gt;<b>/example-project.git</b>
<b>cd example-project</b>
<b>./autogen.sh</b>
</pre>

The <tt>--recursive</tt> is optional because <tt>./autogen.sh</tt> will fix
it when you forgot it.

Afterwards you probably want to use <tt>--enable-mainainer-mode</tt>
as option to the generated <tt>configure</tt> script.

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

Changes to <tt>configure.ac</tt> and <tt>Makefile.am</tt>
are taken care of by <tt>cwm4</tt>, except for linking
which works as usual;

for example, a module that defines a

```
bin_PROGRAMS = foobar
```

would also define

```
foobar_CXXFLAGS = @LIBCWD_R_FLAGS@
foobar_LDADD = ../events/libevents.la ../utils/libutils_r.la ../cwds/libcwds_r.la
```

or whatever the path to `events` is, to link with the required submodules,
libraries, and assuming you also use the [cwds](https://github.com/CarloWood/cwds) submodule.

Finally, run

```
./autogen.sh
```

to let cwm4 do its magic, and commit all the changes.

Checkout [ai-statefultask-testsuite](https://github.com/CarloWood/ai-statefultask-testsuite)
for an example of a project that uses this submodule.
