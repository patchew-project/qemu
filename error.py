"""Generic error classes.

This module seeks to provide semantic error classes that are intended to
be used directly by clients when they would like to handle particular
semantic failures (e.g. "failed to connect") without needing to know the
enumeration of possible reasons for that failure.

AQMPError serves as the ancestor for almost all exceptions raised by
this package, and is suitable for use in handling semantic errors from
this library. In most cases, individual methods will attempt to catch
and re-encapsulate various exceptions to provide a semantic
error-handling interface, though this is not necessarily true of
internal interfaces.

Some errors are not defined here in this module, but exist alongside
more specific error domains in other modules. They are listed here for
convenience anyway.

The error inheritance tree is as follows::

  MultiException
  AQMPError
    ProtocolError
      RawProtocolError
        DeserializationError
        UnexpectedTypeError
      GreetingError
      NegotiationError
      MsgProtocolError   (message.py)
        ObjectTypeError  (message.py)
        OrphanedError    (message.py)
        ServerParseError (message.py)
    ConnectError
    DisconnectedError
    StateError

The only exception that is not an `AQMPError` is `MultiException`. It is
special, and used to encapsulate one-or-more exceptions of an arbitrary
kind; this exception MAY be raised on disconnect() when there are two or
more exceptions from the AQMP event loop to report back to the caller.

(The bottom half is designed in such a way that exceptions are attempted
to be handled internally, but in cases of catastrophic failure, it may
still occur.)

See `MultiException` and `AsyncProtocol.disconnect()` for more details.

"""

from typing import Iterable, Iterator


class AQMPError(Exception):
    # Don't use this directly: create a subclass.
    """Base failure for all errors raised by AQMP."""


class ProtocolError(AQMPError):
    """Abstract error class for protocol failures."""
    def __init__(self, error_message: str):
        super().__init__()
        self.error_message = error_message

    def __str__(self) -> str:
        return f"QMP protocol error: {self.error_message}"


class RawProtocolError(ProtocolError):
    """
    Abstract error class for low-level parsing failures.
    """
    def __init__(self, error_message: str, raw: bytes):
        super().__init__(error_message)
        self.raw = raw

    def __str__(self) -> str:
        return "\n".join([
            super().__str__(),
            f"  raw bytes were: {str(self.raw)}",
        ])


class DeserializationError(RawProtocolError):
    """Incoming message was not understood as JSON."""


class UnexpectedTypeError(RawProtocolError):
    """Incoming message was JSON, but not a JSON object."""


class ConnectError(AQMPError):
    """
    Initial connection process failed.
    Always wraps a "root cause" exception that can be interrogated for info.
    """


class GreetingError(ProtocolError):
    """An exception occurred during the Greeting phase."""
    def __init__(self, error_message: str, exc: Exception):
        super().__init__(error_message)
        self.exc = exc

    def __str__(self) -> str:
        return (
            f"QMP protocol error: {self.error_message}\n"
            f"  Cause: {self.exc!s}\n"
        )


class NegotiationError(ProtocolError):
    """An exception occurred during the Negotiation phase."""
    def __init__(self, error_message: str, exc: Exception):
        super().__init__(error_message)
        self.exc = exc

    def __str__(self) -> str:
        return (
            f"QMP protocol error: {self.error_message}\n"
            f"  Cause: {self.exc!s}\n"
        )


class DisconnectedError(AQMPError):
    """
    Command was not able to be completed; we have been Disconnected.

    This error is raised in response to a pending execution when the
    back-end is unable to process responses any more.
    """


class StateError(AQMPError):
    """
    An API command (connect, execute, etc) was issued at an inappropriate time.

    (e.g. execute() while disconnected; connect() while connected; etc.)
    """


class MultiException(Exception):
    """
    Used for multiplexing exceptions.

    This exception is used in the case that errors were encountered in both the
    Reader and Writer tasks, and we must raise more than one.
    """
    def __init__(self, exceptions: Iterable[BaseException]):
        super().__init__(exceptions)
        self.exceptions = list(exceptions)

    def __str__(self) -> str:
        ret = "------------------------------\n"
        ret += "Multiple Exceptions occurred:\n"
        ret += "\n"
        for i, exc in enumerate(self.exceptions):
            ret += f"{i}) {str(exc)}\n"
            ret += "\n"
        ret += "-----------------------------\n"
        return ret

    def __iter__(self) -> Iterator[BaseException]:
        return iter(self.exceptions)
