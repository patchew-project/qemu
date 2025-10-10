# This work is licensed under the terms of the GNU GPL, version 2.
# See the COPYING file in the top-level directory.
"""
QAPI Rust generator
"""

import os
import re
import subprocess
from typing import NamedTuple, Optional

from .common import POINTER_SUFFIX
from .gen import QAPIGen
from .schema import QAPISchemaModule, QAPISchemaVisitor


# see to_upper_case()/to_lower_case() below
snake_case = re.compile(r'((?<=[a-z0-9])[A-Z]|(?!^)[A-Z](?=[a-z]))')


rs_name_trans = str.maketrans('.-', '__')


# Map @name to a valid Rust identifier.
# If @protect, avoid returning certain ticklish identifiers (like
# keywords) by prepending raw identifier prefix 'r#'.
def rs_name(name: str, protect: bool = True) -> str:
    name = name.translate(rs_name_trans)
    if name[0].isnumeric():
        name = '_' + name
    if not protect:
        return name
    # based from the list:
    # https://doc.rust-lang.org/reference/keywords.html
    if name in ('Self', 'abstract', 'as', 'async',
                'await', 'become', 'box', 'break',
                'const', 'continue', 'crate', 'do',
                'dyn', 'else', 'enum', 'extern',
                'false', 'final', 'fn', 'for',
                'if', 'impl', 'in', 'let',
                'loop', 'macro', 'match', 'mod',
                'move', 'mut', 'override', 'priv',
                'pub', 'ref', 'return', 'self',
                'static', 'struct', 'super', 'trait',
                'true', 'try', 'type', 'typeof',
                'union', 'unsafe', 'unsized', 'use',
                'virtual', 'where', 'while', 'yield'):
        name = 'r#' + name
    # avoid some clashes with the standard library
    if name in ('String',):
        name = 'Qapi' + name

    return name


def rs_type(c_type: str,
            qapi_ns: str = 'qapi::',
            optional: bool = False,
            box: bool = False) -> str:
    (is_pointer, _, is_list, c_type) = rs_ctype_parse(c_type)
    to_rs = {
        'QNull': '()',
        'QObject': 'QObject',
        'any': 'QObject',
        'bool': 'bool',
        'char': 'i8',
        'double': 'f64',
        'int': 'i64',
        'int16': 'i16',
        'int16_t': 'i16',
        'int32': 'i32',
        'int32_t': 'i32',
        'int64': 'i64',
        'int64_t': 'i64',
        'int8': 'i8',
        'int8_t': 'i8',
        'number': 'f64',
        'size': 'u64',
        'str': 'String',
        'uint16': 'u16',
        'uint16_t': 'u16',
        'uint32': 'u32',
        'uint32_t': 'u32',
        'uint64': 'u64',
        'uint64_t': 'u64',
        'uint8': 'u8',
        'uint8_t': 'u8',
        'String': 'QapiString',
    }
    if is_pointer:
        to_rs.update({
            'char': 'String',
        })

    if is_list:
        c_type = c_type[:-4]

    ret = to_rs.get(c_type, qapi_ns + c_type)
    if is_list:
        ret = 'Vec<%s>' % ret
    elif is_pointer and c_type not in to_rs and box:
        ret = 'Box<%s>' % ret
    if optional:
        ret = 'Option<%s>' % ret
    return ret


class CType(NamedTuple):
    is_pointer: bool
    is_const: bool
    is_list: bool
    c_type: str


def rs_ctype_parse(c_type: str) -> CType:
    is_pointer = False
    if c_type.endswith(POINTER_SUFFIX):
        is_pointer = True
        c_type = c_type[:-len(POINTER_SUFFIX)]
    is_list = c_type.endswith('List')
    is_const = False
    if c_type.startswith('const '):
        is_const = True
        c_type = c_type[6:]

    c_type = rs_name(c_type)
    return CType(is_pointer, is_const, is_list, c_type)


def to_camel_case(value: str) -> str:
    # special case for last enum value
    if value == '_MAX':
        return value
    raw_id = False
    if value.startswith('r#'):
        raw_id = True
        value = value[2:]
    value = ''.join('_' + word if word[0].isdigit()
                    else word[:1].upper() + word[1:]
                    for word in filter(None, re.split("[-_]+", value)))
    if raw_id:
        return 'r#' + value
    return value


def to_upper_case(value: str) -> str:
    return snake_case.sub(r'_\1', value).upper()


def to_lower_case(value: str) -> str:
    return snake_case.sub(r'_\1', value).lower()


class QAPIGenRs(QAPIGen):
    pass


class QAPISchemaRsVisitor(QAPISchemaVisitor):

    def __init__(self, prefix: str, what: str):
        super().__init__()
        self._prefix = prefix
        self._what = what
        self._gen = QAPIGenRs(self._prefix + self._what + '.rs')
        self._main_module: Optional[str] = None

    def visit_module(self, name: Optional[str]) -> None:
        if name is None:
            return
        if QAPISchemaModule.is_user_module(name):
            if self._main_module is None:
                self._main_module = name

    def write(self, output_dir: str) -> None:
        self._gen.write(output_dir)

        pathname = os.path.join(output_dir, self._gen.fname)
        try:
            subprocess.check_call(['rustfmt', pathname])
        except FileNotFoundError:
            pass
