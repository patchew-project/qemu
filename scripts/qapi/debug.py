"""
Small debugging facilities for mypy static analysis work.
(C) 2020 John Snow, for Red Hat, Inc.
"""

import inspect
import json
from typing import Dict, List, Any
from types import FrameType


OBSERVED_TYPES: Dict[str, List[str]] = {}


# You have no idea how long it took to find this return type...
def caller_frame() -> FrameType:
    """
    Returns the stack frame of the caller's caller.
    e.g. foo() -> caller() -> caller_frame() return's foo's stack frame.
    """
    stack = inspect.stack()
    caller = stack[2].frame
    if caller is None:
        msg = "Python interpreter does not support stack frame inspection"
        raise RuntimeError(msg)
    return caller


def _add_type_record(name: str, typestr: str) -> None:
    seen = OBSERVED_TYPES.setdefault(name, [])
    if typestr not in seen:
        seen.append(typestr)


def record_type(name: str, value: Any, dict_names: bool = False) -> None:
    """
    Record the type of a variable.

    :param name: The name of the variable
    :param value: The value of the variable
    """
    _add_type_record(name, str(type(value)))

    try:
        for key, subvalue in value.items():
            subname = f"{name}.{key}" if dict_names else f"{name}.[dict_value]"
            _add_type_record(subname, str(type(subvalue)))
        return
    except AttributeError:
        # (Wasn't a dict or anything resembling one.)
        pass

    # str is iterable, but not in the way we want!
    if isinstance(value, str):
        return

    try:
        for elem in value:
            _add_type_record(f"{name}.[list_elem]", str(type(elem)))
    except TypeError:
        # (Wasn't a list or anything else iterable.)
        pass


def show_types() -> None:
    """
    Print all of the currently known variable types to stdout.
    """
    print(json.dumps(OBSERVED_TYPES, indent=2))


def record_locals(show: bool = False, dict_names: bool = False) -> None:
    caller = caller_frame()
    name = caller.f_code.co_name
    for key, value in caller.f_locals.items():
        record_type(f"{name}.{key}", value, dict_names=dict_names)
    if show:
        show_types()
