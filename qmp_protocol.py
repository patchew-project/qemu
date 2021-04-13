"""
QMP Client Implementation

This module provides the QMP class, which can be used to connect and
send commands to a QMP server such as QEMU. The QMP class can be used to
either connect to a listening server, or used to listen and accept an
incoming connection from the server.
"""

import asyncio
import logging
from typing import (
    Awaitable,
    Callable,
    Dict,
    List,
    Mapping,
    Optional,
    Tuple,
    cast,
)

from error import (
    AQMPError,
    DisconnectedError,
    DeserializationError,
    GreetingError,
    NegotiationError,
    StateError,
    UnexpectedTypeError,
)
from message import (
    Message,
    ObjectTypeError,
    ServerParseError,
)
from models import (
    ErrorInfo,
    ErrorResponse,
    Greeting,
    ParsingError,
    ServerResponse,
    SuccessResponse,
)
from protocol import AsyncProtocol
from util import create_task, pretty_traceback


class ExecuteError(AQMPError):
    """Execution statement returned failure."""
    def __init__(self,
                 sent: Message,
                 received: Message,
                 error: ErrorInfo):
        super().__init__()
        self.sent = sent
        self.received = received
        self.error = error

    def __str__(self) -> str:
        return self.error.desc


_EventCallbackFn = Callable[['QMP', Message], Awaitable[None]]


class QMP(AsyncProtocol[Message]):
    """
    Implements a QMP connection to/from the server.

    Basic usage looks like this::

      qmp = QMP('my_virtual_machine_name')
      await qmp.connect(('127.0.0.1', 1234))
      ...
      res = await qmp.execute('block-query')
      ...
      await qmp.disconnect()

    :param name: Optional nickname for the connection, used for logging.
    """
    #: Logger object for debugging messages
    logger = logging.getLogger(__name__)

    def __init__(self, name: Optional[str] = None) -> None:
        super().__init__(name)

        # Greeting
        self.await_greeting = True
        self._greeting: Optional[Greeting] = None
        self.greeting_timeout = 5  # (In seconds)

        # RFC: Do I even want to use any timeouts internally? They're
        # not defined in the protocol itself. Theoretically, a client
        # could simply use asyncio.wait_for(qmp.connect(...), timeout=5)
        # and then I don't have to support this interface at all.
        #
        # We don't need to support any timeouts so long as we never initiate
        # any long-term wait that wasn't in direct response to a user action.

        # Command ID counter
        self._execute_id = 0

        # Event handling
        self._event_queue: asyncio.Queue[Message] = asyncio.Queue()
        self._event_callbacks: List[_EventCallbackFn] = []

        # Incoming RPC reply messages
        self._pending: Dict[str, Tuple[
            asyncio.Future[object],
            asyncio.Queue[Message]]] = {}

    def on_event(self, func: _EventCallbackFn) -> _EventCallbackFn:
        """
        FIXME: Quick hack: decorator to register event handlers.

        Use it like this::

          @qmp.on_event
          async def my_event_handler(qmp, event: Message) -> None:
            print(f"Received event: {event['event']}")

        RFC: What kind of event handler would be the most useful in
        practical terms? In tests, we are usually waiting for an
        event with some criteria to occur; maybe it would be useful
        to allow "coroutine" style functions where we can block
        until a certain event shows up?
        """
        if func not in self._event_callbacks:
            self._event_callbacks.append(func)
        return func

    async def _new_session(self, coro: Awaitable[None]) -> None:
        self._event_queue = asyncio.Queue()
        await super()._new_session(coro)

    async def _on_connect(self) -> None:
        """
        Wait for the QMP greeting prior to the engagement of the full loop.

        :raise: GreetingError when the greeting is not understood.
        """
        if self.await_greeting:
            self._greeting = await self._get_greeting()

    async def _on_start(self) -> None:
        """
        Perform QMP negotiation right after the loop starts.

        Negotiation is performed afterwards so that the implementation
        can simply use `execute()`, which relies on the loop machinery
        to be running.

        :raise: NegotiationError if the negotiation fails in some way.
        """
        await self._negotiate()

    async def _get_greeting(self) -> Greeting:
        """
        :raise: GreetingError  (Many causes.)
        """
        self.logger.debug("Awaiting greeting ...")
        try:
            msg = await asyncio.wait_for(self._recv(), self.greeting_timeout)
            return Greeting.parse_msg(msg)
        except Exception as err:
            if isinstance(err, (asyncio.TimeoutError, OSError, EOFError)):
                emsg = "Failed to receive Greeting"
            elif isinstance(err, (DeserializationError, UnexpectedTypeError)):
                emsg = "Failed to understand Greeting"
            elif isinstance(err, ObjectTypeError):
                emsg = "Failed to validate Greeting"
            else:
                emsg = "Unknown failure acquiring Greeting"

            self.logger.error("%s:\n%s\n", emsg, pretty_traceback())
            raise GreetingError(emsg, err) from err

    async def _negotiate(self) -> None:
        """
        :raise: NegotiationError  (Many causes.)
        """
        self.logger.debug("Negotiating capabilities ...")
        arguments: Dict[str, List[str]] = {'enable': []}
        if self._greeting and 'oob' in self._greeting.QMP.capabilities:
            arguments['enable'].append('oob')
        try:
            await self.execute('qmp_capabilities', arguments=arguments)
        except Exception as err:
            # FIXME: what exceptions do we actually expect execute to raise?
            emsg = "Failure negotiating capabilities"
            self.logger.error("%s:\n%s\n", emsg, pretty_traceback())
            raise NegotiationError(emsg, err) from err

    async def _bh_disconnect(self) -> None:
        # See AsyncProtocol._bh_disconnect().
        await super()._bh_disconnect()

        if self._pending:
            self.logger.debug("Cancelling pending executions")
        for key in self._pending:
            self.logger.debug("Cancelling execution %s", key)
            # NB: This signals cancellation, but doesn't fully quiesce;
            # it merely requests the cancellation; it will be thrown into
            # that tasks's context on the next event loop cycle.
            #
            # This task is being awaited on by `_execute()`, which will
            # exist in the user's callstack in the upper-half. Since
            # we're here, we know it isn't running! It won't have a
            # chance to run again except to receive a cancellation.
            #
            # NB: Python 3.9 adds a msg= parameter to cancel that would
            # be useful for debugging the 'cause' of cancellations.
            self._pending[key][0].cancel()

        self.logger.debug("QMP Disconnected.")

    async def _on_message(self, msg: Message) -> None:
        """
        Add an incoming message to the appropriate queue/handler.

        :raise: RawProtocolError     (`_recv` via `Message._deserialize`)
        :raise: ServerParseError     (Message has no 'event' nor 'id' field)
        """
        # Incoming messages are not fully parsed/validated here;
        # do only light peeking to know how to route the messages.

        if 'event' in msg:
            await self._event_queue.put(msg)
            # FIXME: quick hack; event queue handling.
            for func in self._event_callbacks:
                await func(self, msg)
            return

        # Below, we assume everything left is an execute/exec-oob response.

        if 'id' in msg:
            exec_id = str(msg['id'])
            if exec_id not in self._pending:
                # qmp-spec.txt, section 2.4:
                # 'Clients should drop all the responses
                #  that have an unknown "id" field.'
                self.logger.warning("Unknown ID '%s', response dropped.",
                                    exec_id)
                return
        else:
            # This is a server parsing error;
            # It inherently does not "belong" to any pending execution.
            # Instead of performing clever recovery, just terminate.
            raise ServerParseError(
                "Server sent a message without an ID,"
                " indicating parse failure.", msg)

        _, queue = self._pending[exec_id]
        await queue.put(msg)

    async def _do_recv(self) -> Message:
        """
        :raise: OSError            (Stream errors)
        :raise: `EOFError`         (When the stream is at EOF)
        :raise: `RawProtocolError` (via `Message._deserialize`)

        :return: A single QMP `Message`.
        """
        msg_bytes = await self._readline()
        msg = Message(msg_bytes, eager=True)
        return msg

    def _do_send(self, msg: Message) -> None:
        """
        :raise: ValueError  (JSON serialization failure)
        :raise: TypeError   (JSON serialization failure)
        :raise: OSError     (Stream errors)
        """
        assert self._writer is not None
        self._writer.write(bytes(msg))

    def _cleanup(self) -> None:
        super()._cleanup()
        self._greeting = None
        assert self._pending == {}
        self._event_queue = asyncio.Queue()

    @classmethod
    def make_execute_msg(cls, cmd: str,
                         arguments: Optional[Mapping[str, object]] = None,
                         oob: bool = False) -> Message:
        """
        Create an executable message to be sent by `execute_msg` later.

        :param cmd: QMP command name.
        :param arguments: Arguments (if any). Must be JSON-serializable.
        :param oob: If true, execute "out of band".

        :return: An executable QMP message.
        """
        msg = Message({'exec-oob' if oob else 'execute': cmd})
        if arguments is not None:
            msg['arguments'] = arguments
        return msg

    async def _bh_execute(self, msg: Message,
                          queue: 'asyncio.Queue[Message]') -> object:
        """
        Execute a QMP Message and wait for the result.

        :param msg: Message to execute.
        :param queue: The queue we should expect to see a reply delivered to.

        :return: Execution result from the server.
                 The type depends on the command sent.
        """
        if not self.running:
            raise StateError("QMP is not running.")
        assert self._outgoing

        self._outgoing.put_nowait(msg)
        reply_msg = await queue.get()

        # May raise ObjectTypeError (Unlikely - only if it has missing keys.)
        reply = ServerResponse.parse_msg(reply_msg).__root__
        assert not isinstance(reply, ParsingError)  # Handled by BH

        if isinstance(reply, ErrorResponse):
            # Server indicated execution failure.
            raise ExecuteError(msg, reply_msg, reply.error)

        assert isinstance(reply, SuccessResponse)
        return reply.return_

    async def _execute(self, msg: Message) -> object:
        """
        The same as `execute_msg()`, but without safety mechanisms.

        Does not assign an execution ID and does not check that the form
        of the message being sent is valid.

        This method *Requires* an 'id' parameter to be set on the
        message, it will not set one for you like `execute()` or
        `execute_msg()`.

        Do not use "__aqmp#00000" style IDs, use something else to avoid
        potential clashes. If this ID clashes with an ID presently
        in-use or otherwise clashes with the auto-generated IDs, the
        response routing mechanisms in _on_message may very well fail
        loudly enough to cause the entire loop to crash.

        The ID should be a str; or at least something JSON
        serializable. It *must* be hashable.
        """
        exec_id = cast(str, msg['id'])
        self.logger.debug("Execute(%s): '%s'", exec_id,
                          msg.get('execute', msg.get('exec-oob')))

        queue: asyncio.Queue[Message] = asyncio.Queue(maxsize=1)
        task = create_task(self._bh_execute(msg, queue))
        self._pending[exec_id] = (task, queue)

        try:
            result = await task
        except asyncio.CancelledError as err:
            raise DisconnectedError("Disconnected") from err
        finally:
            del self._pending[exec_id]

        return result

    async def execute_msg(self, msg: Message) -> object:
        """
        Execute a QMP message and return the response.

        :param msg: The QMP `Message` to execute.
        :raises: ValueError if the QMP `Message` does not have either the
                 'execute' or 'exec-oob' fields set.
        :raises: ExecuteError if the server returns an error response.
        :raises: DisconnectedError if the connection was terminated early.

        :return: Execution response from the server. The type of object depends
                 on the command that was issued, though most return a dict.
        """
        if not ('execute' in msg or 'exec-oob' in msg):
            raise ValueError("Requires 'execute' or 'exec-oob' message")
        if self.disconnecting:
            raise StateError("QMP is disconnecting/disconnected."
                             " Call disconnect() to fully disconnect.")

        # FIXME: Copy the message here, to avoid leaking the ID back out.

        exec_id = f"__aqmp#{self._execute_id:05d}"
        msg['id'] = exec_id
        self._execute_id += 1

        return await self._execute(msg)

    async def execute(self, cmd: str,
                      arguments: Optional[Mapping[str, object]] = None,
                      oob: bool = False) -> object:
        """
        Execute a QMP command and return the response.

        :param cmd: QMP command name.
        :param arguments: Arguments (if any). Must be JSON-serializable.
        :param oob: If true, execute "out of band".

        :raise: ExecuteError if the server returns an error response.
        :raise: DisconnectedError if the connection was terminated early.

        :return: Execution response from the server. The type of object depends
                 on the command that was issued, though most return a dict.
        """
        # Note: I designed arguments to be its own argument instead of
        # kwparams so that we are able to add other modifiers that
        # change execution parameters later on. A theoretical
        # higher-level API that is generated against a particular QAPI
        # Schema should generate function signatures the way we want at
        # that point; modifying those commands to behave differently
        # could be performed using context managers that alter the QMP
        # loop for any commands that occur within that block.
        msg = self.make_execute_msg(cmd, arguments, oob=oob)
        return await self.execute_msg(msg)
