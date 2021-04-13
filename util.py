"""
Misc. utils and helper functions
"""

import asyncio
import traceback
import sys
from typing import (
    Any,
    Coroutine,
    TypeVar,
)


T = TypeVar('T')


def create_task(coro: Coroutine[Any, Any, T]) -> 'asyncio.Future[T]':
    """
    Python 3.6-compatible create_task() wrapper.
    """
    if hasattr(asyncio, 'create_task'):
        # Python 3.7+
        return asyncio.create_task(coro)

    # Python 3.6
    return asyncio.ensure_future(coro)


async def wait_closed(writer: asyncio.StreamWriter) -> None:
    """
    Python 3.6-compatible StreamWriter.wait_closed() wrapper.
    """
    if hasattr(writer, 'wait_closed'):
        # Python 3.7+
        await writer.wait_closed()
    else:
        # Python 3.6
        transport = writer.transport
        assert isinstance(transport, asyncio.WriteTransport)

        while not transport.is_closing():
            await asyncio.sleep(0.0)
        while transport.get_write_buffer_size() > 0:
            await asyncio.sleep(0.0)


def asyncio_run(coro: Coroutine[Any, Any, T]) -> T:
    """
    Python 3.6-compatible asyncio.run() wrapper.
    """
    # Python 3.7+
    if hasattr(asyncio, 'run'):
        return asyncio.run(coro)

    # Python 3.6
    loop = asyncio.get_event_loop()
    ret = loop.run_until_complete(coro)
    loop.close()

    return ret


def pretty_traceback() -> str:
    """
    Print the current traceback, but indented to provide visual distinction.

    This is useful for printing a traceback within a traceback for
    debugging purposes when encapsulating errors to deliver them up the
    stack; when those errors are printed, this helps provide a nice
    visual grouping to quickly identify the parts of the error that
    belong to the inner exception.

    :returns: A string, formatted something like the following::

      | Traceback (most recent call last):
      |   File "foobar.py", line 42, in arbitrary_example
      |     foo.baz()
      | ArbitraryError: [Errno 42] Something bad happened!

    """
    exc_lines = []
    for chunk in traceback.format_exception(*sys.exc_info()):
        for line in chunk.split("\n"):
            if line:
                exc_lines.append(f"  | {line}")
    return "\n".join(exc_lines)
