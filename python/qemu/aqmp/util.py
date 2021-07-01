"""
Miscellaneous Utilities

This module primarily provides compatibility wrappers for Python 3.6 to
provide some features that otherwise become available in Python 3.7+.
"""

import asyncio
import sys
from typing import (
    Any,
    Coroutine,
    Optional,
    TypeVar,
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
