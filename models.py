"""
QMP message models.

This module provides definitions for several well-defined JSON object
types that are seen in the QMP wire protocol. Using pydantic, these
models also handle the parsing and validation of these objects in order
to provide strict typing guarantees elsewhere in the library.

Notably, it provides these object models:

- `Greeting`: the standard QMP greeting message (and nested children)
- Three types of server RPC response messages:
  - `ErrorResponse`: A failed-execution reply. (Application-level failure)
  - `SuccessResponse`: A successful execution reply.
  - `ParsingError`: A reply indicating the RPC message was not understood.
                  (Library-level failure, or worse.)
- A special pydantic form of the above three; `ServerResponse`,
  used to parse incoming messages.
- `AsynchronousEvent`: A generic event message.
"""

from typing import (
    Any,
    Dict,
    List,
    Type,
    TypeVar,
    Union,
)

from pydantic import BaseModel, Field, root_validator, ValidationError


from message import Message, ObjectTypeError


class MessageBase(BaseModel):
    """
    An abstract pydantic model that represents any QMP object.

    It does not define any fields, so it isn't very useful as a type.
    However, it provides a strictly typed parsing helper that allows
    us to convert from a QMP `Message` object into a specific model,
    so long as that model inherits from this class.
    """
    _T = TypeVar('_T', bound='MessageBase')

    @classmethod
    def parse_msg(cls: Type[_T], obj: Message) -> _T:
        """
        Convert a `Message` into a strictly typed Python object.

        For Messages that do not pass validation, pydantic validation
        errors are encapsulated using the `ValidationError` class.

        :raises: ValidationError when the given Message cannot be
                 validated (and converted into) as an instance of this class.
        """
        try:
            return cls.parse_obj(obj)
        except ValidationError as err:
            raise ObjectTypeError("Message failed validation.", obj) from err


class VersionTriple(BaseModel):
    """
    Mirrors qapi/control.json VersionTriple structure.
    """
    major: int
    minor: int
    micro: int


class VersionInfo(BaseModel):
    """
    Mirrors qapi/control.json VersionInfo structure.
    """
    qemu: VersionTriple
    package: str


class QMPGreeting(BaseModel):
    """
    'QMP' subsection of the protocol greeting.

    Defined in qmp-spec.txt, section 2.2, "Server Greeting".
    """
    version: VersionInfo
    capabilities: List[str]


class Greeting(MessageBase):
    """
    QMP protocol greeting message.

    Defined in qmp-spec.txt, section 2.2, "Server Greeting".
    """
    QMP: QMPGreeting


class ErrorInfo(BaseModel):
    """
    Error field inside of an error response.

    Defined in qmp-spec.txt, section 2.4.2, "error".
    """
    class_: str = Field(None, alias='class')
    desc: str


class ParsingError(MessageBase):
    """
    Parsing error from QMP that omits ID due to failure.

    Implicitly defined in qmp-spec.txt, section 2.4.2, "error".
    """
    error: ErrorInfo


class SuccessResponse(MessageBase):
    """
    Successful execution response.

    Defined in qmp-spec.txt, section 2.4.1, "success".
    """
    return_: Any = Field(None, alias='return')
    id: str  # NB: The spec allows ANY object here. AQMP does not!

    @root_validator(pre=True)
    @classmethod
    def check_return_value(cls,
                           values: Dict[str, object]) -> Dict[str, object]:
        """Enforce that the 'return' key is present, even if it is None."""
        # To pydantic, 'Any' means 'Optional'; force its presence:
        if 'return' not in values:
            raise TypeError("'return' key not present in object.")
        return values


class ErrorResponse(MessageBase):
    """
    Unsuccessful execution response.

    Defined in qmp-spec.txt, section 2.4.2, "error".
    """
    error: ErrorInfo
    id: str  # NB: The spec allows ANY object here. AQMP does not!


class ServerResponse(MessageBase):
    """
    Union type: This object can be any one of the component messages.

    Implicitly defined in qmp-spec.txt, section 2.4, "Commands Responses".
    """
    __root__: Union[SuccessResponse, ErrorResponse, ParsingError]


class EventTimestamp(BaseModel):
    """
    Timestamp field of QMP event, see `AsynchronousEvent`.

    Defined in qmp-spec.txt, section 2.5, "Asynchronous events".
    """
    seconds: int
    microseconds: int


class AsynchronousEvent(BaseModel):
    """
    Asynchronous event message.

    Defined in qmp-spec.txt, section 2.5, "Asynchronous events".
    """
    event: str
    data: Union[List[Any], Dict[str, Any], str, int, float]
    timestamp: EventTimestamp
