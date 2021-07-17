"""
Miscellaneous Utilities

This module provides asyncio utilities and compatibility wrappers for
Python 3.6 to provide some features that otherwise become available in
Python 3.7+.
"""

import asyncio
import sys
from typing import (
    Any,
    Coroutine,
    Optional,
    TypeVar,
    cast,
)


T = TypeVar('T')


# --------------------------
# Section: Utility Functions
# --------------------------


async def flush(writer: asyncio.StreamWriter) -> None:
    """
    Utility function to ensure a StreamWriter is *fully* drained.

    `asyncio.StreamWriter.drain` only promises we will return to below
    the "high-water mark". This function ensures we flush the entire
    buffer -- by setting the high water mark to 0 and then calling
    drain. The flow control limits are restored after the call is
    completed.
    """
    transport = cast(asyncio.WriteTransport, writer.transport)

    # https://github.com/python/typeshed/issues/5779
    low, high = transport.get_write_buffer_limits()  # type: ignore
    transport.set_write_buffer_limits(0, 0)
    try:
        await writer.drain()
    finally:
        transport.set_write_buffer_limits(high, low)


# -------------------------------
# Section: Compatibility Wrappers
# -------------------------------


def create_task(coro: Coroutine[Any, Any, T],
                loop: Optional[asyncio.AbstractEventLoop] = None
                ) -> 'asyncio.Future[T]':
    """
    Python 3.6-compatible `asyncio.create_task` wrapper.

    :param coro: The coroutine to execute in a task.
    :param loop: Optionally, the loop to create the task in.

    :return: An `asyncio.Future` object.
    """
    if sys.version_info >= (3, 7):
        if loop is not None:
            return loop.create_task(coro)
        return asyncio.create_task(coro)  # pylint: disable=no-member

    # Python 3.6:
    return asyncio.ensure_future(coro, loop=loop)


def is_closing(writer: asyncio.StreamWriter) -> bool:
    """
    Python 3.6-compatible `asyncio.StreamWriter.is_closing` wrapper.

    :param writer: The `asyncio.StreamWriter` object.
    :return: `True` if the writer is closing, or closed.
    """
    if sys.version_info >= (3, 7):
        return writer.is_closing()

    # Python 3.6:
    transport = writer.transport
    assert isinstance(transport, asyncio.WriteTransport)
    return transport.is_closing()


async def wait_closed(writer: asyncio.StreamWriter) -> None:
    """
    Python 3.6-compatible `asyncio.StreamWriter.wait_closed` wrapper.

    :param writer: The `asyncio.StreamWriter` to wait on.
    """
    if sys.version_info >= (3, 7):
        await writer.wait_closed()
        return

    # Python 3.6
    transport = writer.transport
    assert isinstance(transport, asyncio.WriteTransport)

    while not transport.is_closing():
        await asyncio.sleep(0)
    await flush(writer)
