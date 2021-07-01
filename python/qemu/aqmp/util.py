"""
Miscellaneous Utilities

This module primarily provides compatibility wrappers for Python 3.6 to
provide some features that otherwise become available in Python 3.7+.

It additionally provides `pretty_traceback()`, used for formatting
tracebacks for inclusion in the logging stream.
"""

import asyncio
import sys
import traceback
from typing import (
    Any,
    Coroutine,
    Optional,
    TypeVar,
    cast,
)


T = TypeVar('T')


def create_task(coro: Coroutine[Any, Any, T],
                loop: Optional[asyncio.AbstractEventLoop] = None
                ) -> 'asyncio.Future[T]':
    """
    Python 3.6-compatible `asyncio.create_task` wrapper.

    :param coro: The coroutine to execute in a task.
    :param loop: Optionally, the loop to create the task in.

    :return: An `asyncio.Future` object.
    """
    # Python 3.7+:
    if sys.version_info >= (3, 7):
        # pylint: disable=no-member
        if loop is not None:
            return loop.create_task(coro)
        return asyncio.create_task(coro)

    # Python 3.6:
    return asyncio.ensure_future(coro, loop=loop)


def is_closing(writer: asyncio.StreamWriter) -> bool:
    """
    Python 3.6-compatible `asyncio.StreamWriter.is_closing` wrapper.

    :param writer: The `asyncio.StreamWriter` object.
    :return: `True` if the writer is closing, or closed.
    """
    if hasattr(writer, 'is_closing'):
        # Python 3.7+
        return writer.is_closing()  # type: ignore

    # Python 3.6:
    transport = writer.transport
    assert isinstance(transport, asyncio.WriteTransport)
    return transport.is_closing()


async def flush(writer: asyncio.StreamWriter) -> None:
    """
    Utility function to ensure a StreamWriter is *fully* drained.

    `asyncio.StreamWriter.drain` only promises we will return to below
    the "high-water mark". This function ensures we flush the entire
    buffer.
    """
    transport = cast(asyncio.WriteTransport, writer.transport)

    while transport.get_write_buffer_size() > 0:
        await writer.drain()


async def wait_closed(writer: asyncio.StreamWriter) -> None:
    """
    Python 3.6-compatible `asyncio.StreamWriter.wait_closed` wrapper.

    :param writer: The `asyncio.StreamWriter` to wait on.
    """
    if hasattr(writer, 'wait_closed'):
        # Python 3.7+
        await writer.wait_closed()  # type: ignore
    else:
        # Python 3.6
        transport = writer.transport
        assert isinstance(transport, asyncio.WriteTransport)

        while not transport.is_closing():
            await asyncio.sleep(0.0)
        while transport.get_write_buffer_size() > 0:
            await asyncio.sleep(0.0)


async def wait_task_done(task: Optional['asyncio.Future[Any]']) -> None:
    """
    Await a task to finish, but do not consume its result.

    :param task: The task to await completion for.
    """
    while True:
        if task and not task.done():
            await asyncio.sleep(0)  # Yield
        else:
            break


def asyncio_run(coro: Coroutine[Any, Any, T]) -> T:
    """
    Python 3.6-compatible `asyncio.run` wrapper.

    :param coro: A coroutine to execute now.
    :return: The return value from the coroutine.
    """
    # Python 3.7+
    if hasattr(asyncio, 'run'):
        # pylint: disable=no-member
        return asyncio.run(coro)  # type: ignore

    # Python 3.6
    loop = asyncio.get_event_loop()
    ret = loop.run_until_complete(coro)
    loop.close()

    return ret


def pretty_traceback(prefix: str = "  | ") -> str:
    """
    Formats the current traceback, indented to provide visual distinction.

    This is useful for printing a traceback within a traceback for
    debugging purposes when encapsulating errors to deliver them up the
    stack; when those errors are printed, this helps provide a nice
    visual grouping to quickly identify the parts of the error that
    belong to the inner exception.

    :param prefix: The prefix to append to each line of the traceback.
    :return: A string, formatted something like the following::

      | Traceback (most recent call last):
      |   File "foobar.py", line 42, in arbitrary_example
      |     foo.baz()
      | ArbitraryError: [Errno 42] Something bad happened!
    """
    output = "".join(traceback.format_exception(*sys.exc_info()))

    exc_lines = []
    for line in output.split('\n'):
        exc_lines.append(prefix + line)

    # The last line is always empty, omit it
    return "\n".join(exc_lines[:-1])


def upper_half(func: T) -> T:
    """
    Do-nothing decorator that annotates a method as an "upper-half" method.

    These methods must not call bottom-half functions directly, but can
    schedule them to run.
    """
    return func


def bottom_half(func: T) -> T:
    """
    Do-nothing decorator that annotates a method as a "bottom-half" method.

    These methods must take great care to handle their own exceptions whenever
    possible. If they go unhandled, they will cause termination of the loop.

    These methods do not, in general, have the ability to directly
    report information to a caller's context and will usually be
    collected as a Task result instead.

    They must not call upper-half functions directly.
    """
    return func
