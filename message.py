"""
QMP Message format and errors.

This module provides the `Message` class, which represents a single QMP
message sent to or from the server. Several error-classes that depend on
knowing the format of this message are also included here.
"""

import json
from json import JSONDecodeError
from typing import (
    Dict,
    ItemsView,
    Iterable,
    KeysView,
    Optional,
    Union,
    ValuesView,
)

from error import (
    DeserializationError,
    ProtocolError,
    UnexpectedTypeError,
)


class Message:
    """
    Represents a single QMP protocol message.

    QMP uses JSON objects as its basic communicative unit; so this
    object behaves like a MutableMapping. It may be instantiated from
    either another mapping (like a dict), or from raw bytes that still
    need to be deserialized.

    :param value: Initial value, if any.
    :param eager: When true, attempt to serialize (or deserialize) the
                  initial value immediately, such that conversion exceptions
                  are raised during the call to the initialization method.
    """
    # TODO: make Message properly a MutableMapping so it can be typed as such?
    def __init__(self,
                 value: Union[bytes, Dict[str, object]] = b'', *,
                 eager: bool = True):
        self._data: Optional[bytes] = None
        self._obj: Optional[Dict[str, object]] = None

        if isinstance(value, bytes):
            self._data = value
            if eager:
                self._obj = self._deserialize(self._data)
        else:
            self._obj = value
            if eager:
                self._data = self._serialize(self._obj)

    @classmethod
    def _serialize(cls, value: object) -> bytes:
        """
        Serialize a JSON object as bytes.

        :raises: ValueError, TypeError from the json library.
        """
        return json.dumps(value, separators=(',', ':')).encode('utf-8')

    @classmethod
    def _deserialize(cls, data: bytes) -> Dict[str, object]:
        """
        Deserialize JSON bytes into a native python dict.

        :raises: DeserializationError if JSON deserialization
                 fails for any reason.
        :raises: UnexpectedTypeError if data does not represent
                 a JSON object.
        """
        try:
            obj = json.loads(data)
        except JSONDecodeError as err:
            emsg = "Failed to deserialize QMP message."
            raise DeserializationError(emsg, data) from err
        if not isinstance(obj, dict):
            raise UnexpectedTypeError(
                "Incoming QMP message is not a JSON object.",
                data
            )
        return obj

    @property
    def data(self) -> bytes:
        """
        bytes representing this QMP message.

        Generated on-demand if required.
        """
        if self._data is None:
            self._data = self._serialize(self._obj or {})
        return self._data

    @property
    def _object(self) -> Dict[str, object]:
        """
        dict representing this QMP message.

        Generated on-demand if required; Private because it returns an
        object that could be used to validate the internal state of the
        Message object.
        """
        if self._obj is None:
            self._obj = self._deserialize(self._data or b'')
        return self._obj

    def __str__(self) -> str:
        """Pretty-printed representation of this QMP message."""
        return json.dumps(self._object, indent=2)

    def __bytes__(self) -> bytes:
        return self.data

    def __contains__(self, item: str) -> bool:  # Container, Collection
        return item in self._object

    def __iter__(self) -> Iterable[str]:  # Iterable, Collection, Mapping
        return iter(self._object)

    def __len__(self) -> int:  # Sized, Collection, Mapping
        return len(self._object)

    def __getitem__(self, key: str) -> object:  # Mapping
        return self._object[key]

    def __setitem__(self, key: str, value: object) -> None:  # MutableMapping
        self._object[key] = value
        self._data = None

    def __delitem__(self, key: str) -> None:  # MutableMapping
        del self._object[key]
        self._data = None

    def keys(self) -> KeysView[str]:
        """Return a KeysView object containing all field names."""
        return self._object.keys()

    def items(self) -> ItemsView[str, object]:
        """Return an ItemsView object containing all key:value pairs."""
        return self._object.items()

    def values(self) -> ValuesView[object]:
        """Return a ValuesView object containing all field values."""
        return self._object.values()

    def get(self, key: str,
            default: Optional[object] = None) -> Optional[object]:
        """Get the value for a single key."""
        return self._object.get(key, default)


class MsgProtocolError(ProtocolError):
    """Abstract error class for protocol errors that have a JSON object."""
    def __init__(self, error_message: str, msg: Message):
        super().__init__(error_message)
        self.msg = msg

    def __str__(self) -> str:
        return "\n".join([
            super().__str__(),
            f"  Message was: {str(self.msg)}\n",
        ])


class ObjectTypeError(MsgProtocolError):
    """
    Incoming message was a JSON object, but has an unexpected data shape.

    e.g.: A malformed greeting may cause this error.
    """


# FIXME: Remove this? Current draft simply trashes these replies.

# class OrphanedError(MsgProtocolError):
#     """
#     Received message, but had no queue to deliver it to.
#
#     e.g.: A reply arrives from the server, but the ID does not match any
#     pending execution requests we are aware of.
#     """


class ServerParseError(MsgProtocolError):
    """
    Server sent a `ParsingError` message.

    e.g. A reply arrives from the server, but it is missing the "ID"
    field, which indicates a parsing error on behalf of the server.
    """
