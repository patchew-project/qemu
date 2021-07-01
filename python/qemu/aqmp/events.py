"""
AQMP Events and EventListeners

Asynchronous QMP uses `EventListener` objects to listen for events. An
`EventListener` is a FIFO event queue that can be pre-filtered to listen
for only specific events. Each `EventListener` instance receives its own
copy of events that it hears, so events may be consumed without fear or
worry for depriving other listeners of events they need to hear.


EventListener Tutorial
----------------------

In all of the following examples, we assume that we have a
:py:class:`~qmp_protocol.QMP` object instantiated named ``qmp`` that is
already connected.


`listener()` context blocks with one name
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The most basic usage is by using the `listener()` context manager to
construct them:

.. code:: python

   with qmp.listener('STOP') as listener:
       await qmp.execute('stop')
       await listener.get()

The listener is active only for the duration of the ‘with’ block. This
instance listens only for ‘STOP’ events.


`listener()` context blocks with two or more names
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Multiple events can be selected for by providing any ``Iterable[str]``:

.. code:: python

   with qmp.listener(('STOP', 'RESUME')) as listener:
       await qmp.execute('stop')
       event = await listener.get()
       assert event['event'] == 'STOP'

       await qmp.execute('cont')
       event = await listener.get()
       assert event['event'] == 'RESUME'


`listener()` context blocks with no names
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

By omitting names entirely, you can listen to ALL events.

.. code:: python

   with qmp.listener() as listener:
       await qmp.execute('stop')
       event = await listener.get()
       assert event['event'] == 'STOP'

This isn’t a very good use case for this feature: In a non-trivial
running system, we may not know what event will arrive next. Grabbing
the top of a FIFO queue returning multiple kinds of events may be prone
to error.


Using async iterators to retrieve events
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If you’d like to simply watch what events happen to arrive, you can use
the listener as an async iterator:

.. code:: python

   with qmp.listener() as listener:
       async for event in listener:
           print(f"Event arrived: {event['event']}")

This is analogous to the following code:

.. code:: python

   with qmp.listener() as listener:
       while True:
           event = listener.get()
           print(f"Event arrived: {event['event']}")

This event stream will never end, so these blocks will never terminate.


Using asyncio.Task to concurrently retrieve events
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Since a listener’s event stream will never terminate, it is not likely
useful to use that form in a script. For longer-running clients, we can
create event handlers by using `asyncio.Task` to create concurrent
coroutines:

.. code:: python

   async def print_events(listener):
       try:
           async for event in listener:
               print(f"Event arrived: {event['event']}")
       except asyncio.CancelledError:
           return

   with qmp.listener() as listener:
       task = asyncio.Task(print_events(listener))
       await qmp.execute('stop')
       await qmp.execute('cont')
       task.cancel()
       await task

However, there is no guarantee that these events will be received by the
time we leave this context block. Once the context block is exited, the
listener will cease to hear any new events, and becomes inert.

Be mindful of the timing: the above example will *probably*– but does
not *guarantee*– that both STOP/RESUMED events will be printed. The
example below outlines how to use listeners outside of a context block.


Using `register_listener()` and `remove_listener()`
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To create a listener with a longer lifetime, beyond the scope of a
single block, create a listener and then call `register_listener()`:

.. code:: python

   class MyClient:
       def __init__(self, qmp):
           self.qmp = qmp
           self.listener = EventListener()

       async def print_events(self):
           try:
               async for event in self.listener:
                   print(f"Event arrived: {event['event']}")
           except asyncio.CancelledError:
               return

       async def run(self):
           self.task = asyncio.Task(self.print_events)
           self.qmp.register_listener(self.listener)
           await qmp.execute('stop')
           await qmp.execute('cont')

       async def stop(self):
           self.task.cancel()
           await self.task
           self.qmp.remove_listener(self.listener)

The listener can be deactivated by using `remove_listener()`. When it is
removed, any possible pending events are cleared and it can be
re-registered at a later time.


Using the built-in all events listener
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The :py:class:`~qmp_protocol.QMP` object creates its own default
listener named :py:obj:`~Events.events` that can be used for the same
purpose without having to create your own:

.. code:: python

   async def print_events(listener):
       try:
           async for event in listener:
               print(f"Event arrived: {event['event']}")
       except asyncio.CancelledError:
           return

   task = asyncio.Task(print_events(qmp.events))

   await qmp.execute('stop')
   await qmp.execute('cont')

   task.cancel()
   await task


Using both .get() and async iterators
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The async iterator and `get()` methods pull events from the same FIFO
queue. If you mix the usage of both, be aware: Events are emitted
precisely once per listener.

If multiple contexts try to pull events from the same listener instance,
events are still emitted only precisely once.

This restriction can be lifted by creating additional listeners.


Creating multiple listeners
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Additional `EventListener` objects can be created at-will. Each one
receives its own copy of events, with separate FIFO event queues.

.. code:: python

   my_listener = EventListener()
   qmp.register_listener(my_listener)

   await qmp.execute('stop')
   copy1 = await my_listener.get()
   copy2 = await qmp.events.get()

   assert copy1 == copy2

In this example, we await an event from both a user-created
`EventListener` and the built-in events listener. Both receive the same
event.


Clearing listeners
~~~~~~~~~~~~~~~~~~

`EventListener` objects can be cleared, clearing all events seen thus far:

.. code:: python

   await qmp.execute('stop')
   qmp.events.clear()
   await qmp.execute('cont')
   event = await qmp.events.get()
   assert event['event'] == 'RESUME'

`EventListener` objects are FIFO queues. If events are not consumed,
they will remain in the queue until they are witnessed or discarded via
`clear()`. FIFO queues will be drained automatically upon leaving a
context block, or when calling `remove_listener()`.


Accessing listener history
~~~~~~~~~~~~~~~~~~~~~~~~~~

`EventListener` objects record their history. Even after being cleared,
you can obtain a record of all events seen so far:

.. code:: python

   await qmp.execute('stop')
   await qmp.execute('cont')
   qmp.events.clear()

   assert len(qmp.events.history) == 2
   assert qmp.events.history[0]['event'] == 'STOP'
   assert qmp.events.history[1]['event'] == 'RESUME'

The history is updated immediately and does not require the event to be
witnessed first.


Using event filters
~~~~~~~~~~~~~~~~~~~

`EventListener` objects can be given complex filtering criteria if names
are not sufficient:

.. code:: python

   def job1_filter(event) -> bool:
       event_data = event.get('data', {})
       event_job_id = event_data.get('id')
       return event_job_id == "job1"

   with qmp.listener('JOB_STATUS_CHANGE', job1_filter) as listener:
       await qmp.execute('blockdev-backup', arguments={'job-id': 'job1', ...})
       async for event in listener:
           if event['data']['status'] == 'concluded':
               break

These filters might be most useful when parameterized. `EventListener`
objects expect a function that takes only a single argument (the raw
event, as a `Message`) and returns a bool; True if the event should be
accepted into the stream. You can create a function that adapts this
signature to accept configuration parameters:

.. code:: python

   def job_filter(job_id: str) -> EventFilter:
       def filter(event: Message) -> bool:
           return event['data']['id'] == job_id
       return filter

   with qmp.listener('JOB_STATUS_CHANGE', job_filter('job2')) as listener:
       await qmp.execute('blockdev-backup', arguments={'job-id': 'job2', ...})
       async for event in listener:
           if event['data']['status'] == 'concluded':
               break


Activating an existing listener with `listen()`
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Listeners with complex, long configurations can also be created manually
and activated temporarily by using `listen()` instead of `listener()`:

.. code:: python

   listener = EventListener(('BLOCK_JOB_COMPLETED', 'BLOCK_JOB_CANCELLED',
                             'BLOCK_JOB_ERROR', 'BLOCK_JOB_READY',
                             'BLOCK_JOB_PENDING', 'JOB_STATUS_CHANGE'))

   with qmp.listen(listener):
       await qmp.execute('blockdev-backup', arguments={'job-id': 'job3', ...})
       async for event in listener:
           print(event)
           if event['event'] == 'BLOCK_JOB_COMPLETED':
               break

Any events that are not witnessed by the time the block is left will be
cleared from the queue; entering the block is an implicit
`register_listener()` and leaving the block is an implicit
`remove_listener()`.


Activating multiple existing listeners with `listen()`
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

While `listener()` is only capable of creating a single listener,
`listen()` is capable of activating multiple listeners simultaneously:

.. code:: python

   def job_filter(job_id: str) -> EventFilter:
       def filter(event: Message) -> bool:
           return event['data']['id'] == job_id
       return filter

   jobA = EventListener('JOB_STATUS_CHANGE', job_filter('jobA'))
   jobB = EventListener('JOB_STATUS_CHANGE', job_filter('jobB'))

   with qmp.listen(jobA, jobB):
       qmp.execute('blockdev-create', arguments={'job-id': 'jobA', ...})
       qmp.execute('blockdev-create', arguments={'job-id': 'jobB', ...})

       async for event in jobA.get():
           if event['data']['status'] == 'concluded':
               break
       async for event in jobB.get():
           if event['data']['status'] == 'concluded':
               break


Extending the `EventListener` class
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

In the case that a more specialized `EventListener` is desired to
provide either more functionality or more compact syntax for specialized
cases, it can be extended.

One of the key methods to extend or override is
:py:meth:`~EventListener.accept()`. The default implementation checks an
incoming message for:

1. A qualifying name, if any :py:obj:`~EventListener.names` were
   specified at initialization time
2. That :py:obj:`~EventListener.event_filter()` returns True.

This can be modified however you see fit to change the criteria for
inclusion in the stream.

For convenience, a ``JobListener`` class could be created that simply
bakes in configuration so it does not need to be repeated:

.. code:: python

   class JobListener(EventListener):
       def __init__(self, job_id: str):
           super().__init__(('BLOCK_JOB_COMPLETED', 'BLOCK_JOB_CANCELLED',
                             'BLOCK_JOB_ERROR', 'BLOCK_JOB_READY',
                             'BLOCK_JOB_PENDING', 'JOB_STATUS_CHANGE'))
           self.job_id = job_id

       def accept(self, event) -> bool:
           if not super().accept(event):
               return False
           if event['event'] in ('BLOCK_JOB_PENDING', 'JOB_STATUS_CHANGE'):
               return event['data']['id'] == job_id
           return event['data']['device'] == job_id

From here on out, you can conjure up a custom-purpose listener that
listens only for job-related events for a specific job-id easily:

.. code:: python

   listener = JobListener('job4')
   with qmp.listener(listener):
       await qmp.execute('blockdev-backup', arguments={'job-id': 'job4', ...})
       async for event in listener:
           print(event)
           if event['event'] == 'BLOCK_JOB_COMPLETED':
               break


Experimental Interfaces & Design Issues
---------------------------------------

These interfaces are not ones I am sure I will keep or otherwise modify
heavily.

Tertiary, or post-accept filtering
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Primarily filtering is based on the names of events, and secondary
filtering is achieved through the use of event_filter callbacks.

Tertiary filtering occurs after a listener has already accepted the
event, and takes place during the `get()` call.

`get()` accepts optional ``**kwargs`` arguments that get matched
against the ``data`` field of an event to allow for trivial event
conditions:

.. code:: python

   with qmp.listen('JOB_STATUS_CHANGE') as listener:
       await qmp.execute('blockdev-backup', arguments={'job-id': 'job5', ...})
       await listener.get(status='pending')
       await qmp.execute('job-finalize', arguments={'job-id': 'job5', ...})
       await listener.get(status='null')

The problem with this is that the tertiary filtering will drop events
that were not selected for on the floor, which reintroduces some of the
same problems that inspired the creation of `EventListener` to begin
with.

Another problem is that this tertiary filtering is extremely
rudimentary: it is quite convenient for a listener configured to listen
only to ``JOB_STATUS_CHANGE``, but it does not allow for post-selection
of events with different names in the event that a listener was created
with a fairly wide selection criteria.

A final problem is that the filtering is not very powerful: it matches
only fields in ``data`` for strict equality; it cannot perform subset
matching like the legacy `QEMUMachine` methods `event_wait()`,
`events_wait()`, or `event_match()`.

However, those interfaces are … ugly, and a little complicated. They got
the job done years ago when I wrote them, but I think they’re overly
complex and too hard to use now.

Still, I am left wondering if this isn’t flexible enough.

-  Dropping post-filtered events on the floor seems prone to error.
-  Post-filtering on event name(s) might be nice, but further increases
   risk related with accidentally discarding events.
-  Post-filters could (perhaps) return a sequence of events they’ve
   discarded, but that complicates the signature of `get()` a lot:

   .. code:: python

      event, discarded = await listener.get(status='null')
      event, _ = await listener.get(status='null')

-  Maybe listeners could simply cache a “discarded” list into its object
   state, and (possibly) emit a warning if these discarded events are
   not cleared before the listener is unregistered. Still, the goals of compact
   syntax and safety are at odds here. Instructing `get()` that we're OK
   with tossing events on the floor every time we use it will quickly clutter
   up most unit tests.

-  ``kwargs`` syntax is convenient for the job filtering case in particular,
   but is not really broadly flexible.

-  Maybe post-filtering can also be done with event filter functions,
   the same kind as used for secondary pre-filtering. It’d at least
   allow for maximum flexibility – but the syntax would be less
   convenient and compact than the kwargs post-filters:

   .. code:: python

      def event_filter(event) -> bool:
          return event['data']['status'] == 'null'

      event = await listener.get(event_filter)

-  The above suggestion also introduces a complexity if we want to
   support both the ``**kwargs`` form and the ``event_filter`` form:
   Whatever name is chosen for the ``event_filter`` argument implicitly
   prohibits us from filtering against any possible data fields of the
   same name.

   Items beginning with "__" are prohibited in the QMP spec, though, so
   it may be safe to name the event filter argument something like
   "__filter".

   Python 3.8’s PEP570 “Positional Only Parameters”
   https://www.python.org/dev/peps/pep-0570/ would be a good fit for
   this feature, but we will not be able to use it for quite some time
   in QEMU. (We will not be able to use 3.7 until some time in 2022.)


qmp.listener()’s type signature
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

`listener()` does not return anything, because it was assumed the caller
already had a handle to the listener. However, for
``qmp.listener(EventListener())`` forms, the caller will not have saved
a handle to the listener.

Because this function can accept *many* listeners, I found it hard to
accurately type in a way where it could be used in both “one” or “many”
forms conveniently and in a statically type-safe manner.

Ultimately, I removed the return altogether, but perhaps with more time
I can work out a way to re-add it.


listener-dispatched callbacks
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

An earlier design allowed for users to directly set a callback on a
listener.

It also allowed for a decorator to be used to easily morph a given
function into an event callback for an event of the same name:

.. code:: python

   @qmp.event
   async def stop(event):
       print("QEMU has stopped!")

Or to manually specify a list of events the handler was written for:

.. code:: python

   @qmp.event(('STOP', 'RESUME'))
   async def handler(event):
       print(f"Got event '{event['event']}'!")

This was very convenient for setting up dedicated functions that handle
specific events, setting up all-events loggers, etc. I didn't like this
in the end, for a few reasons:

-  When setting a callback on a listener, it meant that `get()` and the
   async iterator became dead interfaces that would never return
   anything. It felt like an abuse of the interface, ultimately. Forcing
   the user to retrieve the event themselves felt like the “cleaner”
   architecture – though severely less convenient.

-  If the EventListener itself was responsible for executing an event
   callback, it meant that the QMP bottom half itself was ultimately
   responsible for calling user callbacks which may fault and cause the
   bottom half to terminate.

   I didn't like the idea of a QMP client loop dying because of user
   code; Normally, the design of the bottom half is such that “internal”
   errors are hidden from the caller. In this case, the caller is likely
   the only one who actually understands the error, which felt like an
   inversion of concerns.


API Reference
-------------

"""

import asyncio
from contextlib import contextmanager
import logging
from typing import (
    AsyncIterator,
    Callable,
    Iterable,
    Iterator,
    List,
    Mapping,
    Optional,
    Set,
    Tuple,
    Union,
    cast,
)

from .error import AQMPError
from .message import Message


EventNames = Union[str, Iterable[str], None]
EventFilter = Callable[[Message], bool]


class ListenerError(AQMPError):
    """
    Generic error class for `EventListener`-related problems.
    """


class EventListener:
    """
    Selectively listens for events with runtime configurable filtering.

    This class is designed to be directly usable for the most common cases,
    but it can be extended to provide more rigorous control.

    :param names:
        One or more names of events to listen for.
        When not provided, listen for ALL events.
    :param event_filter:
        An optional event filtering function.
        When names are also provided, this acts as a secondary filter.

    When ``names`` and ``event_filter`` are both provided, the names
    will be filtered first, and then the filter function will be called
    second. The event filter function can assume that the format of the
    event is a known format.
    """
    def __init__(
        self,
        names: EventNames = None,
        event_filter: Optional[EventFilter] = None,
    ):
        # Queue of 'heard' events yet to be witnessed by a caller.
        self._queue: 'asyncio.Queue[Message]' = asyncio.Queue()

        # Intended as a historical record, NOT a processing queue or backlog.
        self._history: List[Message] = []

        #: Primary event filter, based on one or more event names.
        self.names: Set[str] = set()
        if isinstance(names, str):
            self.names.add(names)
        elif names is not None:
            self.names.update(names)

        #: Optional, secondary event filter.
        self.event_filter: Optional[EventFilter] = event_filter

    @property
    def history(self) -> Tuple[Message, ...]:
        """
        A read-only history of all events seen so far.

        This represents *every* event, including those not yet witnessed
        via `get()` or ``async for``. It persists between `clear()`
        calls and is immutable.
        """
        return tuple(self._history)

    async def _get(self) -> Message:
        """
        Wait for the very next event in this stream.

        If one is already available, return that one.
        """
        return await self._queue.get()

    def accept(self, event: Message) -> bool:
        """
        Determine if this listener accepts this event.

        This method determines which events will appear in the stream.
        The default implementation simply checks the event against the
        list of names and the event_filter to decide if this
        `EventListener` accepts a given event. It can be
        overridden/extended to provide custom listener behavior.

        User code is not expected to need to invoke this method.

        :param event: The event under consideration.
        :return: `True`, if this listener accepts this event.
        """
        name_ok = (not self.names) or (event['event'] in self.names)
        return name_ok and (
            (not self.event_filter) or self.event_filter(event)
        )

    async def put(self, event: Message) -> None:
        """
        Conditionally put a new event into the FIFO queue.

        This method is not designed to be invoked from user code, and it
        should not need to be overridden. It is a public interface so
        that :py:class:`~qmp_protocol.QMP` has an interface by which it
        can inform registered listeners of new events.

        The event will be put into the queue if
        :py:meth:`~EventListener.accept()` returns `True`.

        :param event: The new event to put into the FIFO queue.
        """
        if not self.accept(event):
            return

        self._history.append(event)
        await self._queue.put(event)

    async def get(self, **kwargs: object) -> Message:
        r"""
        Wait for an event with optional tertiary filtering.

        :param \*\*kwargs: Optional tertiary filtering criteria. Each
            keyword provided is treated as a key belonging to the
            event's 'data' field. The value provided is matched against
            the candidate event's data members.

        .. warning:: When tertiary filtering criteria are provided,
            events that do not match tertiary criteria will be silently
            dropped by this listener. All events that were accepted by
            the listener will be visible in `history()`.
        """
        if not kwargs:
            return await self._get()

        def _tertiary_filter(event: Message) -> bool:
            data = cast(Mapping[str, object], event.get('data', {}))
            for key, value in kwargs.items():
                if key not in data:
                    return False
                if data[key] != value:
                    return False
            return True

        async for event in self:
            if _tertiary_filter(event):
                return event
        else:
            assert False  # Should be impossible to reach.

    def clear(self) -> None:
        """
        Clear this listener of all pending events.

        Called when an `EventListener` is being unregistered, this clears the
        pending FIFO queue synchronously. It can be also be used to
        manually clear any pending events, if desired.

        .. warning::
            Take care when discarding events. Cleared events will be
            silently tossed on the floor. All events that were ever
            accepted by this listener are visible in `history()`.
        """
        while True:
            try:
                self._queue.get_nowait()
            except asyncio.QueueEmpty:
                break

    def __aiter__(self) -> AsyncIterator[Message]:
        return self

    async def __anext__(self) -> Message:
        """
        Enables the `EventListener` to function as an async iterator.

        It may be used like this:

        .. code:: python

            async for event in listener:
                print(event)

        These iterators will never terminate of their own accord; you
        must provide break conditions or otherwise prepare to run them
        in an `asyncio.Task` that can be cancelled.
        """
        return await self._get()


class Events:
    """
    Events is a mix-in class that adds event functionality to the QMP class.

    It's designed specifically as a mix-in for
    :py:class:`~qmp_protocol.QMP`, and it relies upon the class it is
    being mixed into having a 'logger' property.
    """
    def __init__(self) -> None:
        self._listeners: List[EventListener] = []

        #: Default, all-events `EventListener`.
        self.events: EventListener = EventListener()
        self.register_listener(self.events)

        # Parent class needs to have a logger
        self.logger: logging.Logger

    async def _event_dispatch(self, msg: Message) -> None:
        """
        Given a new event, propagate it to all of the active listeners.

        :param msg: The event to propagate.
        """
        for listener in self._listeners:
            await listener.put(msg)

    def register_listener(self, listener: EventListener) -> None:
        """
        Register and activate an `EventListener`.

        :param listener: The listener to activate.
        :raise ListenerError: If the given listener is already registered.
        """
        if listener in self._listeners:
            raise ListenerError("Attempted to re-register existing listener")
        self.logger.debug("Registering %s.", str(listener))
        self._listeners.append(listener)

    def remove_listener(self, listener: EventListener) -> None:
        """
        Unregister and deactivate an `EventListener`.

        The removed listener will have its pending events cleared via
        `clear()`. The listener can be re-registered later when
        desired.

        :param listener: The listener to deactivate.
        :raise ListenerError: If the given listener is not registered.
        """
        if listener == self.events:
            raise ListenerError("Cannot remove the default listener.")
        self.logger.debug("Removing %s.", str(listener))
        listener.clear()
        self._listeners.remove(listener)

    @contextmanager
    def listen(self, *listeners: EventListener) -> Iterator[None]:
        r"""
        Context manager: Temporarily listen with an `EventListener`.

        Accepts one or more `EventListener` objects and registers them,
        activating them for the duration of the context block.

        `EventListener` objects will have any pending events in their
        FIFO queue cleared upon exiting the context block, when they are
        deactivated.

        :param \*listeners: One or more EventListeners to activate.
        :raise ListenerError: If the given listener(s) are already active.
        """
        _added = []

        try:
            for listener in listeners:
                self.register_listener(listener)
                _added.append(listener)

            yield

        finally:
            for listener in _added:
                self.remove_listener(listener)

    @contextmanager
    def listener(
        self,
        names: EventNames = (),
        event_filter: Optional[EventFilter] = None
    ) -> Iterator[EventListener]:
        """
        Context manager: Temporarily listen with a new `EventListener`.

        Creates an `EventListener` object and registers it, activating
        it for the duration of the context block.

        :param names:
            One or more names of events to listen for.
            When not provided, listen for ALL events.
        :param event_filter:
            An optional event filtering function.
            When names are also provided, this acts as a secondary filter.

        :return: The newly created and active `EventListener`.
        """
        listener = EventListener(names, event_filter)
        with self.listen(listener):
            yield listener
