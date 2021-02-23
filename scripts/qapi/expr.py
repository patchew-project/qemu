# -*- coding: utf-8 -*-
#
# Check (context-free) QAPI schema expression structure
#
# Copyright IBM, Corp. 2011
# Copyright (c) 2013-2019 Red Hat Inc.
#
# Authors:
#  Anthony Liguori <aliguori@us.ibm.com>
#  Markus Armbruster <armbru@redhat.com>
#  Eric Blake <eblake@redhat.com>
#  Marc-André Lureau <marcandre.lureau@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.
# See the COPYING file in the top-level directory.

import re
from typing import (
    Iterable,
    List,
    MutableMapping,
    Optional,
    Union,
    cast,
)

from .common import c_name
from .error import QAPISemError
from .parser import QAPIDoc
from .source import QAPISourceInfo


# Arbitrary form for a JSON-like object.
_JSObject = MutableMapping[str, object]
# Expressions in their raw form are (just) JSON-like objects.
Expression = _JSObject


# Names must be letters, numbers, -, and _.  They must start with letter,
# except for downstream extensions which must start with __RFQDN_.
# Dots are only valid in the downstream extension prefix.
valid_name = re.compile(r'^(__[a-zA-Z0-9.-]+_)?'
                        '[a-zA-Z][a-zA-Z0-9_-]*$')


def check_name_is_str(name: object,
                      info: QAPISourceInfo,
                      source: str) -> None:
    if not isinstance(name, str):
        raise QAPISemError(info, "%s requires a string name" % source)


def check_name_str(name: str,
                   info: QAPISourceInfo,
                   source: str,
                   allow_optional: bool = False,
                   enum_member: bool = False,
                   permit_upper: bool = False) -> None:
    membername = name

    if allow_optional and name.startswith('*'):
        membername = name[1:]
    # Enum members can start with a digit, because the generated C
    # code always prefixes it with the enum name
    if enum_member and membername[0].isdigit():
        membername = 'D' + membername
    # Reserve the entire 'q_' namespace for c_name(), and for 'q_empty'
    # and 'q_obj_*' implicit type names.
    if not valid_name.match(membername) or \
       c_name(membername, False).startswith('q_'):
        raise QAPISemError(info, "%s has an invalid name" % source)
    if not permit_upper and name.lower() != name:
        raise QAPISemError(
            info, "%s uses uppercase in name" % source)
    assert not membername.startswith('*')


def check_defn_name_str(name: str, info: QAPISourceInfo, meta: str) -> None:
    check_name_str(name, info, meta, permit_upper=True)
    if name.endswith('Kind') or name.endswith('List'):
        raise QAPISemError(
            info, "%s name should not end in '%s'" % (meta, name[-4:]))


def check_keys(value: _JSObject,
               info: QAPISourceInfo,
               source: str,
               required: List[str],
               optional: List[str]) -> None:

    def pprint(elems: Iterable[str]) -> str:
        return ', '.join("'" + e + "'" for e in sorted(elems))

    missing = set(required) - set(value)
    if missing:
        raise QAPISemError(
            info,
            "%s misses key%s %s"
            % (source, 's' if len(missing) > 1 else '',
               pprint(missing)))
    allowed = set(required + optional)
    unknown = set(value) - allowed
    if unknown:
        raise QAPISemError(
            info,
            "%s has unknown key%s %s\nValid keys are %s."
            % (source, 's' if len(unknown) > 1 else '',
               pprint(unknown), pprint(allowed)))


def check_flags(expr: Expression, info: QAPISourceInfo) -> None:
    for key in ['gen', 'success-response']:
        if key in expr and expr[key] is not False:
            raise QAPISemError(
                info, "flag '%s' may only use false value" % key)
    for key in ['boxed', 'allow-oob', 'allow-preconfig', 'coroutine']:
        if key in expr and expr[key] is not True:
            raise QAPISemError(
                info, "flag '%s' may only use true value" % key)
    if 'allow-oob' in expr and 'coroutine' in expr:
        # This is not necessarily a fundamental incompatibility, but
        # we don't have a use case and the desired semantics isn't
        # obvious.  The simplest solution is to forbid it until we get
        # a use case for it.
        raise QAPISemError(info, "flags 'allow-oob' and 'coroutine' "
                                 "are incompatible")


def check_if(expr: _JSObject, info: QAPISourceInfo, source: str) -> None:

    ifcond = expr.get('if')
    if ifcond is None:
        return

    # Normalize to a list
    if not isinstance(ifcond, list):
        ifcond = [ifcond]
        expr['if'] = ifcond

    if not ifcond:
        raise QAPISemError(info, f"'if' condition [] of {source} is useless")

    for element in ifcond:
        if not isinstance(element, str):
            raise QAPISemError(info, (
                f"'if' condition of {source}"
                " must be a string or a list of strings"))
        if element.strip() == '':
            raise QAPISemError(
                info, f"'if' condition '{element}' of {source} makes no sense")


def normalize_members(members: object) -> None:
    if isinstance(members, dict):
        for key, arg in members.items():
            if isinstance(arg, dict):
                continue
            members[key] = {'type': arg}


def check_type(value: Optional[object],
               info: QAPISourceInfo,
               source: str,
               allow_array: bool = False,
               allow_dict: Union[bool, str] = False) -> None:
    if value is None:
        return

    # Type name
    if isinstance(value, str):
        return

    # Array type
    if isinstance(value, list):
        if not allow_array:
            raise QAPISemError(info, "%s cannot be an array" % source)
        if len(value) != 1 or not isinstance(value[0], str):
            raise QAPISemError(info,
                               "%s: array type must contain single type name" %
                               source)
        return

    # Anonymous type

    if not allow_dict:
        raise QAPISemError(info, "%s should be a type name" % source)

    if not isinstance(value, dict):
        raise QAPISemError(info,
                           "%s should be an object or type name" % source)

    permit_upper = False
    if isinstance(allow_dict, str):
        permit_upper = allow_dict in info.pragma.name_case_whitelist

    # value is a dictionary, check that each member is okay
    for (key, arg) in value.items():
        key_source = "%s member '%s'" % (source, key)
        check_name_str(key, info, key_source,
                       allow_optional=True, permit_upper=permit_upper)
        if c_name(key, False) == 'u' or c_name(key, False).startswith('has_'):
            raise QAPISemError(info, "%s uses reserved name" % key_source)
        check_keys(arg, info, key_source, ['type'], ['if', 'features'])
        check_if(arg, info, key_source)
        check_features(arg.get('features'), info)
        check_type(arg['type'], info, key_source, allow_array=True)


def check_features(features: Optional[object],
                   info: QAPISourceInfo) -> None:
    if features is None:
        return
    if not isinstance(features, list):
        raise QAPISemError(info, "'features' must be an array")
    features[:] = [f if isinstance(f, dict) else {'name': f}
                   for f in features]
    for f in features:
        source = "'features' member"
        assert isinstance(f, dict)
        check_keys(f, info, source, ['name'], ['if'])
        check_name_is_str(f['name'], info, source)
        source = "%s '%s'" % (source, f['name'])
        check_name_str(f['name'], info, source)
        check_if(f, info, source)


def check_enum(expr: Expression, info: QAPISourceInfo) -> None:
    name = expr['enum']
    members = expr['data']
    prefix = expr.get('prefix')

    if not isinstance(members, list):
        raise QAPISemError(info, "'data' must be an array")
    if prefix is not None and not isinstance(prefix, str):
        raise QAPISemError(info, "'prefix' must be a string")

    permit_upper = name in info.pragma.name_case_whitelist

    members[:] = [m if isinstance(m, dict) else {'name': m}
                  for m in members]
    for member in members:
        source = "'data' member"
        check_keys(member, info, source, ['name'], ['if'])
        check_name_is_str(member['name'], info, source)
        source = "%s '%s'" % (source, member['name'])
        check_name_str(member['name'], info, source,
                       enum_member=True, permit_upper=permit_upper)
        check_if(member, info, source)


def check_struct(expr: Expression, info: QAPISourceInfo) -> None:
    name = cast(str, expr['struct'])  # Asserted in check_exprs
    members = expr['data']

    check_type(members, info, "'data'", allow_dict=name)
    check_type(expr.get('base'), info, "'base'")


def check_union(expr: Expression, info: QAPISourceInfo) -> None:
    name = cast(str, expr['union'])  # Asserted in check_exprs
    base = expr.get('base')
    discriminator = expr.get('discriminator')
    members = expr['data']

    if discriminator is None:   # simple union
        if base is not None:
            raise QAPISemError(info, "'base' requires 'discriminator'")
    else:                       # flat union
        check_type(base, info, "'base'", allow_dict=name)
        if not base:
            raise QAPISemError(info, "'discriminator' requires 'base'")
        check_name_is_str(discriminator, info, "'discriminator'")

    if not isinstance(members, dict):
        raise QAPISemError(info, "'data' must be an object")

    for (key, value) in members.items():
        source = "'data' member '%s'" % key
        check_name_str(key, info, source)
        check_keys(value, info, source, ['type'], ['if'])
        check_if(value, info, source)
        check_type(value['type'], info, source, allow_array=not base)


def check_alternate(expr: Expression, info: QAPISourceInfo) -> None:
    members = expr['data']

    if not members:
        raise QAPISemError(info, "'data' must not be empty")

    if not isinstance(members, dict):
        raise QAPISemError(info, "'data' must be an object")

    for (key, value) in members.items():
        source = "'data' member '%s'" % key
        check_name_str(key, info, source)
        check_keys(value, info, source, ['type'], ['if'])
        check_if(value, info, source)
        check_type(value['type'], info, source)


def check_command(expr: Expression, info: QAPISourceInfo) -> None:
    args = expr.get('data')
    rets = expr.get('returns')
    boxed = expr.get('boxed', False)

    if boxed and args is None:
        raise QAPISemError(info, "'boxed': true requires 'data'")
    check_type(args, info, "'data'", allow_dict=not boxed)
    check_type(rets, info, "'returns'", allow_array=True)


def check_event(expr: Expression, info: QAPISourceInfo) -> None:
    args = expr.get('data')
    boxed = expr.get('boxed', False)

    if boxed and args is None:
        raise QAPISemError(info, "'boxed': true requires 'data'")
    check_type(args, info, "'data'", allow_dict=not boxed)


def check_exprs(exprs: List[_JSObject]) -> List[_JSObject]:
    for expr_elem in exprs:
        # Expression
        assert isinstance(expr_elem['expr'], dict)
        for key in expr_elem['expr'].keys():
            assert isinstance(key, str)
        expr: Expression = expr_elem['expr']

        # QAPISourceInfo
        assert isinstance(expr_elem['info'], QAPISourceInfo)
        info: QAPISourceInfo = expr_elem['info']

        # Optional[QAPIDoc]
        tmp = expr_elem.get('doc')
        assert tmp is None or isinstance(tmp, QAPIDoc)
        doc: Optional[QAPIDoc] = tmp

        if 'include' in expr:
            continue

        if 'enum' in expr:
            meta = 'enum'
        elif 'union' in expr:
            meta = 'union'
        elif 'alternate' in expr:
            meta = 'alternate'
        elif 'struct' in expr:
            meta = 'struct'
        elif 'command' in expr:
            meta = 'command'
        elif 'event' in expr:
            meta = 'event'
        else:
            raise QAPISemError(info, "expression is missing metatype")

        name = cast(str, expr[meta])  # Asserted right below:
        check_name_is_str(name, info, "'%s'" % meta)
        info.set_defn(meta, name)
        check_defn_name_str(name, info, meta)

        if doc:
            if doc.symbol != name:
                raise QAPISemError(
                    info, "documentation comment is for '%s'" % doc.symbol)
            doc.check_expr(expr)
        elif info.pragma.doc_required:
            raise QAPISemError(info,
                               "documentation comment required")

        if meta == 'enum':
            check_keys(expr, info, meta,
                       ['enum', 'data'], ['if', 'features', 'prefix'])
            check_enum(expr, info)
        elif meta == 'union':
            check_keys(expr, info, meta,
                       ['union', 'data'],
                       ['base', 'discriminator', 'if', 'features'])
            normalize_members(expr.get('base'))
            normalize_members(expr['data'])
            check_union(expr, info)
        elif meta == 'alternate':
            check_keys(expr, info, meta,
                       ['alternate', 'data'], ['if', 'features'])
            normalize_members(expr['data'])
            check_alternate(expr, info)
        elif meta == 'struct':
            check_keys(expr, info, meta,
                       ['struct', 'data'], ['base', 'if', 'features'])
            normalize_members(expr['data'])
            check_struct(expr, info)
        elif meta == 'command':
            check_keys(expr, info, meta,
                       ['command'],
                       ['data', 'returns', 'boxed', 'if', 'features',
                        'gen', 'success-response', 'allow-oob',
                        'allow-preconfig', 'coroutine'])
            normalize_members(expr.get('data'))
            check_command(expr, info)
        elif meta == 'event':
            check_keys(expr, info, meta,
                       ['event'], ['data', 'boxed', 'if', 'features'])
            normalize_members(expr.get('data'))
            check_event(expr, info)
        else:
            assert False, 'unexpected meta type'

        check_if(expr, info, meta)
        check_features(expr.get('features'), info)
        check_flags(expr, info)

    return exprs
