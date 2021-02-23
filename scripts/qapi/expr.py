# -*- coding: utf-8 -*-
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

"""
Normalize and validate (context-free) QAPI schema expression structures.

After QAPI expressions are parsed from disk, they are stored in
recursively nested Python data structures using Dict, List, str, bool,
and int. This module ensures that those nested structures have the
correct type(s) and key(s) where appropriate for the QAPI context-free
grammar.

The QAPI schema expression language allows for syntactic sugar; this
module also handles the normalization process of these nested
structures.

See `check_exprs` for the main entry point.

See `schema.QAPISchema` for processing into native Python data
structures and contextual semantic validation.
"""

import re
from typing import (
    Collection,
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
#: Expressions in their unvalidated form are JSON-like objects.
Expression = _JSObject


# Names must be letters, numbers, -, and _.  They must start with letter,
# except for downstream extensions which must start with __RFQDN_.
# Dots are only valid in the downstream extension prefix.
valid_name = re.compile(r'^(__[a-zA-Z0-9.-]+_)?'
                        '[a-zA-Z][a-zA-Z0-9_-]*$')


def check_name_is_str(name: object,
                      info: QAPISourceInfo,
                      source: str) -> None:
    """Ensures that ``name`` is a string. [Const]"""
    if not isinstance(name, str):
        raise QAPISemError(info, "%s requires a string name" % source)


def check_name_str(name: str,
                   info: QAPISourceInfo,
                   source: str,
                   allow_optional: bool = False,
                   enum_member: bool = False,
                   permit_upper: bool = False) -> None:
    """
    Ensures a string is a legal name. [Const]

    A name is legal in the default case when:

    - It matches ``(__[a-z0-9.-]+_)?[a-z][a-z0-9_-]*``
    - It does not start with ``q_`` or ``q-``

    :param name:           Name to check.
    :param info:           QAPI source file information.
    :param source:         Human-readable str describing "what" this name is.
    :param allow_optional: Allow the very first character to be ``*``.
                           (Cannot be used with ``enum_member``)
    :param enum_member:    Allow the very first character to be a digit.
                           (Cannot be used with ``allow_optional``)
    :param permit_upper:   Allows upper-case characters wherever
                           lower-case characters are allowed.
    """
    assert not (allow_optional and enum_member)
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
    """
    Ensures a name is a legal definition name. [Const]

    A legal definition name:
     - Adheres to the criteria in `check_name_str`, with uppercase permitted
     - Does not end with ``Kind`` or ``List``.

    :param name: Name to check.
    :param info: QAPI source file information.
    :param meta: Type name of the QAPI expression.
    """
    check_name_str(name, info, meta, permit_upper=True)
    if name.endswith('Kind') or name.endswith('List'):
        raise QAPISemError(
            info, "%s name should not end in '%s'" % (meta, name[-4:]))


def check_keys(value: _JSObject,
               info: QAPISourceInfo,
               source: str,
               required: Collection[str] = (),
               optional: Collection[str] = ()) -> None:
    """
    Ensures an object has a specific set of keys. [Const]

    :param value:    The object to check.
    :param info:     QAPI source file information.
    :param source:   Human-readable str describing "what" this object is.
    :param required: Keys that *must* be present.
    :param optional: Keys that *may* be present.
    """

    def pprint(elems: Iterable[str]) -> str:
        return ', '.join("'" + e + "'" for e in sorted(elems))

    missing = set(required) - set(value)
    if missing:
        raise QAPISemError(
            info,
            "%s misses key%s %s"
            % (source, 's' if len(missing) > 1 else '',
               pprint(missing)))
    allowed = set(required) | set(optional)
    unknown = set(value) - allowed
    if unknown:
        raise QAPISemError(
            info,
            "%s has unknown key%s %s\nValid keys are %s."
            % (source, 's' if len(unknown) > 1 else '',
               pprint(unknown), pprint(allowed)))


def check_flags(expr: Expression, info: QAPISourceInfo) -> None:
    """
    Ensures common fields in an Expression are correct. [Const]

    :param expr: Expression to validate.
    :param info: QAPI source file information.
    """
    for key in ('gen', 'success-response'):
        if key in expr and expr[key] is not False:
            raise QAPISemError(
                info, "flag '%s' may only use false value" % key)
    for key in ('boxed', 'allow-oob', 'allow-preconfig', 'coroutine'):
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
    """
    Syntactically validate and normalize the ``if`` field of an object. [RW]

    The ``if`` field may be either a ``str`` or a ``List[str]``.
    A ``str`` element will be normalized to ``List[str]``.

    :param expr: A ``dict``; the ``if`` field, if present, will be validated.
    :param info: QAPI source file information.

    :forms:
      :sugared: ``Union[str, List[str]]``
      :canonical: ``List[str]``
    """

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
    """
    Normalize a "members" value. [RW]

    If ``members`` is an object, for every value in that object, if that
    value is not itself already an object, normalize it to
    ``{'type': value}``.

    :forms:
      :sugared: ``Dict[str, Union[str, TypeRef]]``
      :canonical: ``Dict[str, TypeRef]``
    """
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
    """
    Check the QAPI type of ``value``. [RW]

    Python types of ``str`` or ``None`` are always allowed.

    :param value:       The value to check.
    :param info:        QAPI Source file information.
    :param source:      Human readable string describing "what" the value is.
    :param allow_array: Allow a ``List[str]`` of length 1,
                        which indicates an Array<T> type.
    :param allow_dict:  Allow a dict, treated as an anonymous type.
                        When given a string, check if that name is allowed to
                        have keys that use uppercase letters, and modify
                        validation accordingly.
    """
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
    """
    Syntactically validate and normalize the ``features`` field. [RW]

    ``features`` may be a ``list`` of either ``str`` or ``dict``.
    Any ``str`` element will be normalized to ``{'name': element}``.

    :forms:
      :sugared: ``List[Union[str, Feature]]``
      :canonical: ``List[Feature]``
    """
    if features is None:
        return
    if not isinstance(features, list):
        raise QAPISemError(info, "'features' must be an array")
    features[:] = [f if isinstance(f, dict) else {'name': f}
                   for f in features]
    for feature in features:
        source = "'features' member"
        assert isinstance(feature, dict)
        check_keys(feature, info, source, ['name'], ['if'])
        check_name_is_str(feature['name'], info, source)
        source = "%s '%s'" % (source, feature['name'])
        check_name_str(feature['name'], info, source)
        check_if(feature, info, source)


def check_enum(expr: Expression, info: QAPISourceInfo) -> None:
    """
    Validate this `Expression` as an ``enum`` expression. [RW]

    :param expr: `Expression` to validate.
    :param info: QAPI source file information.
    """
    check_keys(expr, info, 'enum',
               required=('enum', 'data'),
               optional=('if', 'features', 'prefix'))

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
    """
    Validate this `Expression` as a ``struct`` expression. [RW]

    :param expr: `Expression` to validate.
    :param info: QAPI source file information.
    """
    check_keys(expr, info, 'struct',
               required=('struct', 'data'),
               optional=('base', 'if', 'features'))
    normalize_members(expr['data'])

    name = cast(str, expr['struct'])  # Asserted in check_exprs
    members = expr['data']

    check_type(members, info, "'data'", allow_dict=name)
    check_type(expr.get('base'), info, "'base'")


def check_union(expr: Expression, info: QAPISourceInfo) -> None:
    """
    Validate this `Expression` as a ``union`` expression. [RW]

    :param expr: `Expression` to validate.
    :param info: QAPI source file information.
    """
    check_keys(expr, info, 'union',
               required=('union', 'data'),
               optional=('base', 'discriminator', 'if', 'features'))

    normalize_members(expr.get('base'))
    normalize_members(expr['data'])

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
    """
    Validate this `Expression` as an ``alternate`` expression. [RW]

    :param expr: Expression to validate.
    :param info: QAPI source file information.
    """
    check_keys(expr, info, 'alternate',
               required=('alternate', 'data'),
               optional=('if', 'features'))
    normalize_members(expr['data'])

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
    """
    Validate this `Expression` as a ``command`` expression. [RW]

    :param expr: `Expression` to validate.
    :param info: QAPI source file information.
    """
    check_keys(expr, info, 'command',
               required=('command',),
               optional=('data', 'returns', 'boxed', 'if', 'features',
                         'gen', 'success-response', 'allow-oob',
                         'allow-preconfig', 'coroutine'))
    normalize_members(expr.get('data'))

    args = expr.get('data')
    rets = expr.get('returns')
    boxed = expr.get('boxed', False)

    if boxed and args is None:
        raise QAPISemError(info, "'boxed': true requires 'data'")
    check_type(args, info, "'data'", allow_dict=not boxed)
    check_type(rets, info, "'returns'", allow_array=True)


def check_event(expr: Expression, info: QAPISourceInfo) -> None:
    """
    Normalize and syntactically validate the ``event`` expression. [RW]

    :Event:
      :event: ``str``
      :data: ``Optional[Dict[str, Member]]``
      :boxed: ``Optional[bool]``
      :if: ``Optional[Ifcond]`` (see: `check_if`)
      :features: ``Optional[Features]`` (see: `check_features`)
    """
    check_keys(expr, info, 'event',
               required=('event',),
               optional=('data', 'boxed', 'if', 'features'))
    normalize_members(expr.get('data'))

    args = expr.get('data')
    boxed = expr.get('boxed', False)

    if boxed and args is None:
        raise QAPISemError(info, "'boxed': true requires 'data'")
    check_type(args, info, "'data'", allow_dict=not boxed)


def check_exprs(exprs: List[_JSObject]) -> List[_JSObject]:
    """
    Validate and normalize a list of parsed QAPI schema expressions. [RW]

    This function accepts a list of expressions + metadta as returned by
    the parser. It destructively normalizes the expressions in-place.

    :param exprs: The list of expressions to normalize/validate.
    :return: The same list of expressions (now modified).
    """
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
            check_enum(expr, info)
        elif meta == 'union':
            check_union(expr, info)
        elif meta == 'alternate':
            check_alternate(expr, info)
        elif meta == 'struct':
            check_struct(expr, info)
        elif meta == 'command':
            check_command(expr, info)
        elif meta == 'event':
            check_event(expr, info)
        else:
            assert False, 'unexpected meta type'

        check_if(expr, info, meta)
        check_features(expr.get('features'), info)
        check_flags(expr, info)

    return exprs
