"""
Generic Asynchronous Message-based Protocol Support

This module provides a generic framework for sending and receiving
messages over an asyncio stream. `AsyncProtocol` is an abstract class
that implements the core mechanisms of a simple send/receive protocol,
and is designed to be extended.

In this package, it is used as the implementation for the
:py:class:`~qmp_protocol.QMP` class.
"""

import asyncio
from asyncio import StreamReader, StreamWriter
from enum import Enum
from functools import wraps
from ssl import SSLContext
from typing import (
    Any,
    Awaitable,
    Callable,
    Generic,
    List,
    Optional,
    Tuple,
    TypeVar,
    Union,
    cast,
)

from .error import AQMPError, MultiException
from .util import (
    bottom_half,
    create_task,
    flush,
    is_closing,
    upper_half,
    wait_closed,
    wait_task_done,
)


T = TypeVar('T')
_TaskFN = Callable[[], Awaitable[None]]  # aka ``async def func() -> None``
_FutureT = TypeVar('_FutureT', bound=Optional['asyncio.Future[Any]'])


class Runstate(Enum):
    """Protocol session runstate."""

    #: Fully quiesced and disconnected.
    IDLE = 0
    #: In the process of connecting or establishing a session.
    CONNECTING = 1
    #: Fully connected and active session.
    RUNNING = 2
    #: In the process of disconnecting.
    #: Runstate may be returned to `IDLE` by calling `disconnect()`.
    DISCONNECTING = 3


class ConnectError(AQMPError):
    """
    Raised when the initial connection process has failed.

    This Exception always wraps a "root cause" exception that can be
    interrogated for additional information.

    :param error_message: Human-readable string describing the error.
    :param exc: The root-cause exception.
    """
    def __init__(self, error_message: str, exc: Exception):
        super().__init__(error_message)
        #: Human-readable error string
        self.error_message: str = error_message
        #: Wrapped root cause exception
        self.exc: Exception = exc

    def __str__(self) -> str:
        return f"{self.error_message}: {self.exc!s}"


class StateError(AQMPError):
    """
    An API command (connect, execute, etc) was issued at an inappropriate time.

    This error is raised when a command like
    :py:meth:`~AsyncProtocol.connect()` is issued at an inappropriate
    time.

    :param error_message: Human-readable string describing the state violation.
    :param state: The actual `Runstate` seen at the time of the violation.
    :param required: The `Runstate` required to process this command.

    """
    def __init__(self, error_message: str,
                 state: Runstate, required: Runstate):
        super().__init__(error_message)
        self.error_message = error_message
        self.state = state
        self.required = required


F = TypeVar('F', bound=Callable[..., Any])  # pylint: disable=invalid-name


# Don't Panic.
def require(required_state: Runstate) -> Callable[[F], F]:
    """
    Decorator: protect a method so it can only be run in a certain `Runstate`.

    :param required_state: The `Runstate` required to invoke this method.
    :raise StateError: When the required `Runstate` is not met.
    """
    def _decorator(func: F) -> F:
        # _decorator is the decorator that is built by calling the
        # require() decorator factory; e.g.:
        #
        # @require(Runstate.IDLE) def # foo(): ...
        # will replace 'foo' with the result of '_decorator(foo)'.

        @wraps(func)
        def _wrapper(proto: 'AsyncProtocol[Any]',
                     *args: Any, **kwargs: Any) -> Any:
            # _wrapper is the function that gets executed prior to the
            # decorated method.

            if proto.runstate != required_state:
                if proto.runstate == Runstate.CONNECTING:
                    emsg = "Client is currently connecting."
                elif proto.runstate == Runstate.DISCONNECTING:
                    emsg = ("Client is disconnecting."
                            " Call disconnect() to return to IDLE state.")
                elif proto.runstate == Runstate.RUNNING:
                    emsg = "Client is already connected and running."
                elif proto.runstate == Runstate.IDLE:
                    emsg = "Client is disconnected and idle."
                else:
                    assert False
                raise StateError(emsg, proto.runstate, required_state)
            # No StateError, so call the wrapped method.
            return func(proto, *args, **kwargs)

        # Return the decorated method;
        # Transforming Func to Decorated[Func].
        return cast(F, _wrapper)

    # Return the decorator instance from the decorator factory. Phew!
    return _decorator


class AsyncProtocol(Generic[T]):
    """
    AsyncProtocol implements a generic async message-based protocol.

    This protocol assumes the basic unit of information transfer between
    client and server is a "message", the details of which are left up
    to the implementation. It assumes the sending and receiving of these
    messages is full-duplex and not necessarily correlated; i.e. it
    supports asynchronous inbound messages.

    It is designed to be extended by a specific protocol which provides
    the implementations for how to read and send messages. These must be
    defined in `_do_recv()` and `_do_send()`, respectively.

    Other callbacks have a default implementation, but are intended to be
    either extended or overridden:

     - `_begin_new_session`:
         The base implementation starts the reader/writer tasks.
         A protocol implementation can override this call, inserting
         actions to be taken prior to starting the reader/writer tasks
         before the super() call; actions needing to occur afterwards
         can be written after the super() call.
     - `_on_message`:
         Actions to be performed when a message is received.
    """
    # pylint: disable=too-many-instance-attributes

    # -------------------------
    # Section: Public interface
    # -------------------------

    def __init__(self) -> None:
        # stream I/O
        self._reader: Optional[StreamReader] = None
        self._writer: Optional[StreamWriter] = None

        # Outbound Message queue
        self._outgoing: asyncio.Queue[T] = asyncio.Queue()

        # Special, long-running tasks:
        self._reader_task: Optional[asyncio.Future[None]] = None
        self._writer_task: Optional[asyncio.Future[None]] = None

        # Aggregate of the above tasks; this is useful for Exception
        # aggregation when leaving the loop and ensuring that all tasks
        # are done.
        self._bh_tasks: Optional[asyncio.Future[Tuple[
            Optional[BaseException],
            Optional[BaseException],
        ]]] = None

        #: Disconnect task. The disconnect implementation runs in a task
        #: so that asynchronous disconnects (initiated by the
        #: reader/writer) are allowed to wait for the reader/writers to
        #: exit.
        self._dc_task: Optional[asyncio.Future[None]] = None

        self._runstate = Runstate.IDLE

        #: An `asyncio.Event` that signals when `runstate` is changed.
        self.runstate_changed: asyncio.Event = asyncio.Event()

    @property
    def runstate(self) -> Runstate:
        """The current `Runstate` of the connection."""
        return self._runstate

    @upper_half
    @require(Runstate.IDLE)
    async def connect(self, address: Union[str, Tuple[str, int]],
                      ssl: Optional[SSLContext] = None) -> None:
        """
        Connect to the server and begin processing message queues.

        If this call fails, `runstate` is guaranteed to be set back to `IDLE`.

        :param address:
            Address to connect to; UNIX socket path or TCP address/port.
        :param ssl: SSL context to use, if any.

        :raise StateError: When the `Runstate` is not `IDLE`.
        :raise ConnectError: If a connection cannot be made to the server.
        """
        await self._new_session(address, ssl)

    @upper_half
    async def disconnect(self, force: bool = False) -> None:
        """
        Disconnect and wait for all tasks to fully stop.

        If there were exceptions that caused the reader/writers to terminate
        prematurely, they will be raised here.

        :param force:
            When `False`, drain the outgoing message queue first before
            terminating execution. When `True`, terminate immediately.

        :raise Exception: When the reader or writer terminate unexpectedly.
        :raise MultiException:
            When both the reader and writer terminate unexpectedly. This
            exception is iterable and multiplexes both root cause exceptions.
        """
        self._schedule_disconnect(force)
        await self._wait_disconnect()

    # --------------------------
    # Section: Session machinery
    # --------------------------

    @upper_half
    @bottom_half
    def _set_state(self, state: Runstate) -> None:
        """
        Change the `Runstate` of the protocol connection.

        Signals the `runstate_changed` event.
        """
        if state == self._runstate:
            return

        self._runstate = state
        self.runstate_changed.set()
        self.runstate_changed.clear()

    @upper_half
    async def _new_session(self,
                           address: Union[str, Tuple[str, int]],
                           ssl: Optional[SSLContext] = None) -> None:
        """
        Establish a new connection and initialize the session.

        Connect or accept a new connection, then begin the protocol
        session machinery. If this call fails, `runstate` is guaranteed
        to be set back to `IDLE`.

        :param address:
            Address to connect to;
            UNIX socket path or TCP address/port.
        :param ssl: SSL context to use, if any.

        :raise ConnectError:
            When a connection or session cannot be established.

            This exception will wrap a more concrete one. In most cases,
            the wrapped exception will be `OSError` or `EOFError`. If a
            protocol-level failure occurs while establishing a new
            session, the wrapped error may also be an `AQMPError`.
        """
        assert self.runstate == Runstate.IDLE
        self._set_state(Runstate.CONNECTING)

        self._outgoing = asyncio.Queue()

        phase = "connection"
        try:
            await self._do_connect(address, ssl)

            phase = "session"
            await self._begin_new_session()

        except Exception as err:
            # Reset from CONNECTING back to IDLE.
            await self.disconnect()
            emsg = f"Failed to establish {phase}"
            raise ConnectError(emsg, err) from err

        assert self.runstate == Runstate.RUNNING

    @upper_half
    async def _do_connect(self, address: Union[str, Tuple[str, int]],
                          ssl: Optional[SSLContext] = None) -> None:
        """
        Acting as the transport client, initiate a connection to a server.

        :param address:
            Address to connect to; UNIX socket path or TCP address/port.
        :param ssl: SSL context to use, if any.

        :raise OSError: For stream-related errors.
        """
        if isinstance(address, tuple):
            connect = asyncio.open_connection(address[0], address[1], ssl=ssl)
        else:
            connect = asyncio.open_unix_connection(path=address, ssl=ssl)
        self._reader, self._writer = await connect

    @upper_half
    async def _begin_new_session(self) -> None:
        """
        After a connection is established, start the bottom half machinery.
        """
        assert self.runstate == Runstate.CONNECTING

        reader_coro = self._bh_loop_forever(self._bh_recv_message)
        writer_coro = self._bh_loop_forever(self._bh_send_message)

        self._reader_task = create_task(reader_coro)
        self._writer_task = create_task(writer_coro)

        self._bh_tasks = asyncio.gather(
            self._reader_task,
            self._writer_task,
            return_exceptions=True,
        )

        self._set_state(Runstate.RUNNING)

    @upper_half
    @bottom_half
    def _schedule_disconnect(self, force: bool = False) -> None:
        """
        Initiate a disconnect; idempotent.

        This method is used both in the upper-half as a direct
        consequence of `disconnect()`, and in the bottom-half in the
        case of unhandled exceptions in the reader/writer tasks.

        It can be invoked no matter what the `runstate` is.

        :param force:
            When `False`, drain the outgoing message queue first before
            terminating execution. When `True`, terminate immediately.
        """
        if not self._dc_task:
            self._dc_task = create_task(self._bh_disconnect(force))

    @upper_half
    def _results(self) -> None:
        """
        Raises exception(s) from the finished tasks, if any.

        Called to fully quiesce the reader/writer. In the event of an
        intentional cancellation by the user that completes gracefully,
        this method will not raise any exceptions.

        In the event that both the reader and writer task should fail,
        a `MultiException` will be raised containing both exceptions.

        :raise Exception:
            Arbitrary exceptions re-raised on behalf of a failing Task.
        :raise MultiException:
            Iterable Exception used to multiplex multiple exceptions in the
            event that multiple Tasks failed with non-cancellation reasons.
        """
        assert self.runstate == Runstate.DISCONNECTING
        exceptions: List[BaseException] = []

        assert self._bh_tasks is None or self._bh_tasks.done()
        results = self._bh_tasks.result() if self._bh_tasks else ()

        for result in results:
            if result is None:
                continue
            exceptions.append(result)

        if len(exceptions) == 1:
            raise exceptions.pop()
        if len(exceptions) > 1:
            # This is possible in theory, but I am not sure if it can
            # occur in practice. The reader could suffer an Exception
            # and then immediately schedule a disconnect. That
            # disconnect is not guaranteed to run immediately, so the
            # writer could be scheduled immediately afterwards instead
            # of the disconnect task, and encounter another exception.
            #
            # An improved solution may be to raise the *first* exception
            # which occurs, leaving the *second* exception to be merely
            # logged. Still, what if the caller wants to interrogate
            # that second exception?
            raise MultiException(exceptions)

    @upper_half
    async def _wait_disconnect(self) -> None:
        """
        Waits for a previously scheduled disconnect to finish.

        This method will gather any bottom half exceptions and
        re-raise them; so it is intended to be used in the upper half
        call chain.

        If a single exception is encountered, it will be re-raised
        faithfully.  If multiple are found, they will be multiplexed
        into a `MultiException`.

        :raise Exception:
            Arbitrary exceptions re-raised on behalf of a failing Task.
        :raise MultiException:
            Iterable Exception used to multiplex multiple exceptions in the
            event that multiple Tasks failed with non-cancellation reasons.
        """
        assert self._dc_task
        await self._dc_task
        self._dc_task = None

        try:
            self._results()  # Raises BH errors here.
        finally:
            self._cleanup()

    @upper_half
    def _cleanup(self) -> None:
        """
        Fully reset this object to a clean state and return to `IDLE`.
        """
        def _paranoid_task_erase(task: _FutureT) -> Optional[_FutureT]:
            # Help to erase a task, ENSURING it is fully quiesced first.
            assert (task is None) or task.done()
            return None if (task and task.done()) else task

        assert self.runstate == Runstate.DISCONNECTING
        self._dc_task = _paranoid_task_erase(self._dc_task)
        self._reader_task = _paranoid_task_erase(self._reader_task)
        self._writer_task = _paranoid_task_erase(self._writer_task)
        self._bh_tasks = _paranoid_task_erase(self._bh_tasks)

        self._reader = None
        self._writer = None

        self._set_state(Runstate.IDLE)

    # ----------------------------
    # Section: Bottom Half methods
    # ----------------------------

    @bottom_half
    async def _bh_disconnect(self, force: bool = False) -> None:
        """
        Disconnect and cancel all outstanding tasks.

        It is designed to be called from its task context,
        :py:obj:`~AsyncProtocol._dc_task`. By running in its own task,
        it is free to wait on any pending actions that may still need to
        occur in either the reader or writer tasks.

        :param force:
            When `False`, drain the outgoing message queue first before
            terminating execution. When `True`, terminate immediately.

        """
        # Prohibit new calls to execute() et al.
        self._set_state(Runstate.DISCONNECTING)

        await self._bh_stop_writer(force)
        await self._bh_stop_reader()

        # Next, close the writer stream itself.
        # This implicitly closes the reader, too.
        if self._writer:
            if not is_closing(self._writer):
                self._writer.close()
            await wait_closed(self._writer)

    @bottom_half
    async def _bh_stop_writer(self, force: bool = False) -> None:
        # If we're not in a hurry, drain the outbound queue.
        if self._writer_task and not self._writer_task.done():
            if not force:
                await self._outgoing.join()
                assert self._writer is not None
                await flush(self._writer)

        # Cancel the writer task.
        if self._writer_task and not self._writer_task.done():
            self._writer_task.cancel()
        await wait_task_done(self._writer_task)

    @bottom_half
    async def _bh_stop_reader(self) -> None:
        if self._reader_task and not self._reader_task.done():
            self._reader_task.cancel()
        await wait_task_done(self._reader_task)

    @bottom_half
    async def _bh_loop_forever(self, async_fn: _TaskFN) -> None:
        """
        Run one of the bottom-half methods in a loop forever.

        If the bottom half ever raises any exception, schedule a
        disconnect that will terminate the entire loop.

        :param async_fn: The bottom-half method to run in a loop.
        """
        try:
            while True:
                await async_fn()
        except asyncio.CancelledError:
            # We have been cancelled by _bh_disconnect, exit gracefully.
            return
        except BaseException:
            self._schedule_disconnect(force=True)
            raise

    @bottom_half
    async def _bh_send_message(self) -> None:
        """
        Wait for an outgoing message, then send it.

        Designed to be run in `_bh_loop_forever()`.
        """
        msg = await self._outgoing.get()
        try:
            await self._send(msg)
        finally:
            self._outgoing.task_done()

    @bottom_half
    async def _bh_recv_message(self) -> None:
        """
        Wait for an incoming message and call `_on_message` to route it.

        Designed to be run in `_bh_loop_forever()`.
        """
        msg = await self._recv()
        await self._on_message(msg)

    # --------------------
    # Section: Message I/O
    # --------------------

    @upper_half
    @bottom_half
    async def _do_recv(self) -> T:
        """
        Abstract: Read from the stream and return a message.

        Very low-level; intended to only be called by `_recv()`.
        """
        raise NotImplementedError

    @upper_half
    @bottom_half
    async def _recv(self) -> T:
        """
        Read an arbitrary protocol message.

        .. warning::
            This method is intended primarily for `_bh_recv_message()`
            to use in an asynchronous task loop. Using it outside of
            this loop will "steal" messages from the normal routing
            mechanism. It is safe to use prior to `_begin_new_session()`,
            but should not be used otherwise.

        This method uses `_do_recv()` to retrieve the raw message, and
        then transforms it using `_cb_inbound()`.

        :return: A single (filtered, processed) protocol message.
        """
        # A forthcoming commit makes this method less trivial.
        return await self._do_recv()

    @upper_half
    @bottom_half
    def _do_send(self, msg: T) -> None:
        """
        Abstract: Write a message to the stream.

        Very low-level; intended to only be called by `_send()`.
        """
        raise NotImplementedError

    @upper_half
    @bottom_half
    async def _send(self, msg: T) -> None:
        """
        Send an arbitrary protocol message.

        This method will transform any outgoing messages according to
        `_cb_outbound()`.

        .. warning::
            Like `_recv()`, this method is intended to be called by
            the writer task loop that processes outgoing
            messages. Calling it directly may circumvent logic
            implemented by the caller meant to correlate outgoing and
            incoming messages.

        :raise OSError: For problems with the underlying stream.
        """
        # A forthcoming commit makes this method less trivial.
        self._do_send(msg)

    @bottom_half
    async def _on_message(self, msg: T) -> None:
        """
        Called to handle the receipt of a new message.

        .. caution::
            This is executed from within the reader loop, so be advised
            that waiting on either the reader or writer task will lead
            to deadlock. Additionally, any unhandled exceptions will
            directly cause the loop to halt, so logic may be best-kept
            to a minimum if at all possible.

        :param msg: The incoming message
        """
        # Nothing to do in the abstract case.
