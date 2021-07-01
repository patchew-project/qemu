"""
AQMP Error Classes

This package seeks to provide semantic error classes that are intended
to be used directly by clients when they would like to handle particular
semantic failures (e.g. "failed to connect") without needing to know the
enumeration of possible reasons for that failure.

AQMPError serves as the ancestor for *almost* all exceptions raised by
this package, and is suitable for use in handling semantic errors from
this library. In most cases, individual public methods will attempt to
catch and re-encapsulate various exceptions to provide a semantic
error-handling interface.

.. caution::

    The only exception that is not an `AQMPError` is
    `MultiException`. It is special, and used to encapsulate one-or-more
    exceptions of an arbitrary kind; this exception MAY be raised on
    `disconnect()` when there are two or more exceptions from the AQMP
    event loop to report back to the caller.

    Every pain has been taken to prevent this circumstance but in
    certain cases these exceptions may occasionally be (unfortunately)
    visible. See `MultiException` and `AsyncProtocol.disconnect()` for
    more details.


.. admonition:: AQMP Exception Hierarchy Reference

 |   `Exception`
 |    +-- `MultiException`
 |    +-- `AQMPError`
 |         +-- `ConnectError`
 |         +-- `StateError`
 |         +-- `ExecInterruptedError`
 |         +-- `ExecuteError`
 |         +-- `ListenerError`
 |         +-- `ProtocolError`
 |              +-- `DeserializationError`
 |              +-- `UnexpectedTypeError`
 |              +-- `ServerParseError`
 |              +-- `BadReplyError`
 |              +-- `GreetingError`
 |              +-- `NegotiationError`
"""

from typing import Iterable, Iterator, List


class AQMPError(Exception):
    """Abstract error class for all errors originating from this package."""


class ProtocolError(AQMPError):
    """
    Abstract error class for protocol failures.

    Semantically, these errors are generally the fault of either the
    protocol server or as a result of a bug in this this library.

    :param error_message: Human-readable string describing the error.
    """
    def __init__(self, error_message: str):
        super().__init__(error_message)
        #: Human-readable error message, without any prefix.
        self.error_message: str = error_message


class MultiException(Exception):
    """
    Used for multiplexing exceptions.

    This exception is used in the case that errors were encountered in both the
    Reader and Writer tasks, and we must raise more than one.

    PEP 0654 seeks to remedy this clunky infrastructure, but it will not be
    available for quite some time -- possibly Python 3.11 or even later.

    :param exceptions: An iterable of `BaseException` objects.
    """
    def __init__(self, exceptions: Iterable[BaseException]):
        super().__init__(exceptions)
        self._exceptions: List[BaseException] = list(exceptions)

    def __str__(self) -> str:
        ret = "------------------------------\n"
        ret += "Multiple Exceptions occurred:\n"
        ret += "\n"
        for i, exc in enumerate(self._exceptions):
            ret += f"{i}) {str(exc)}\n"
            ret += "\n"
        ret += "-----------------------------\n"
        return ret

    def __iter__(self) -> Iterator[BaseException]:
        return iter(self._exceptions)
