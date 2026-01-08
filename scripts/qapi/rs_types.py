# This work is licensed under the terms of the GNU GPL, version 2.
# See the COPYING file in the top-level directory.
"""
QAPI Rust types generator

Copyright (c) 2025 Red Hat, Inc.

This work is licensed under the terms of the GNU GPL, version 2.
See the COPYING file in the top-level directory.
"""

from typing import List, Optional, Set

from .common import (
    camel_to_lower,
    camel_to_upper,
    rs_name,
    to_camel_case,
)
from .rs import QAPISchemaRsVisitor, mcgen
from .schema import (
    QAPISchema,
    QAPISchemaAlternateType,
    QAPISchemaEnumMember,
    QAPISchemaFeature,
    QAPISchemaIfCond,
    QAPISchemaObjectType,
    QAPISchemaObjectTypeMember,
    QAPISchemaType,
    QAPISchemaVariants,
)
from .source import QAPISourceInfo


objects_seen = set()


def gen_rs_variants_to_tag(name: str,
                           ifcond: QAPISchemaIfCond,
                           variants: QAPISchemaVariants) -> str:
    ret = mcgen('''

%(cfg)s
impl From<&%(rs_name)sVariant> for %(tag)s {
    fn from(e: &%(rs_name)sVariant) -> Self {
        match e {
''',
                cfg=ifcond.rsgen(),
                rs_name=rs_name(name),
                tag=variants.tag_member.type.rs_type())

    for var in variants.variants:
        type_name = var.type.name
        tag_name = var.name
        patt = '(_)'
        if type_name == 'q_empty':
            patt = ''
        ret += mcgen('''
            %(cfg)s
            %(rs_name)sVariant::%(var_name)s%(patt)s => Self::%(tag_name)s,
''',
                     cfg=var.ifcond.rsgen(),
                     rs_name=rs_name(name),
                     tag_name=rs_name(camel_to_upper(tag_name)),
                     var_name=rs_name(to_camel_case(tag_name)),
                     patt=patt)

    ret += mcgen('''
        }
    }
}
''')
    return ret


def gen_rs_variants(name: str,
                    ifcond: QAPISchemaIfCond,
                    variants: QAPISchemaVariants) -> str:
    ret = mcgen('''

%(cfg)s
#[derive(Clone, Debug, PartialEq)]
pub enum %(rs_name)sVariant {
''',
                cfg=ifcond.rsgen(),
                rs_name=rs_name(name))

    for var in variants.variants:
        type_name = var.type.name
        var_name = rs_name(to_camel_case(var.name))
        if type_name == 'q_empty':
            ret += mcgen('''
    %(cfg)s
    %(var_name)s,
''',
                         cfg=var.ifcond.rsgen(),
                         var_name=var_name)
        else:
            ret += mcgen('''
    %(cfg)s
    %(var_name)s(%(rs_type)s),
''',
                         cfg=var.ifcond.rsgen(),
                         var_name=var_name,
                         rs_type=var.type.rs_type())

    ret += mcgen('''
}
''')

    ret += gen_rs_variants_to_tag(name, ifcond, variants)

    return ret


def gen_rs_members(members: List[QAPISchemaObjectTypeMember],
                   exclude: Optional[List[str]] = None) -> List[str]:
    exclude = exclude or []
    return [f"{m.ifcond.rsgen()} {rs_name(camel_to_lower(m.name))}"
            for m in members if m.name not in exclude]


def has_recursive_type(memb: QAPISchemaType,
                       name: str,
                       visited: Set[str]) -> bool:
    # pylint: disable=too-many-return-statements
    if name == memb.name:
        return True
    if memb.name in visited:
        return False
    visited.add(memb.name)
    if isinstance(memb, QAPISchemaObjectType):
        if memb.base and has_recursive_type(memb.base, name, visited):
            return True
        if memb.branches and \
                any(has_recursive_type(m.type, name, visited)
                    for m in memb.branches.variants):
            return True
        if any(has_recursive_type(m.type, name, visited)
               for m in memb.members):
            return True
        return any(has_recursive_type(m.type, name, visited)
                   for m in memb.local_members)
    if isinstance(memb, QAPISchemaAlternateType):
        return any(has_recursive_type(m.type, name, visited)
                   for m in memb.alternatives.variants)
    return False


def gen_struct_members(members: List[QAPISchemaObjectTypeMember],
                       name: str) -> str:
    ret = ''
    for memb in members:
        typ = memb.type.rs_type()
        if has_recursive_type(memb.type, name, set()):
            typ = 'Box<%s>' % typ
        if memb.optional:
            typ = 'Option<%s>' % typ
        ret += mcgen('''
    %(cfg)s
    pub %(rs_name)s: %(rs_type)s,
''',
                     cfg=memb.ifcond.rsgen(),
                     rs_type=typ,
                     rs_name=rs_name(camel_to_lower(memb.name)))
    return ret


def gen_rs_object(name: str,
                  ifcond: QAPISchemaIfCond,
                  base: Optional[QAPISchemaObjectType],
                  members: List[QAPISchemaObjectTypeMember],
                  variants: Optional[QAPISchemaVariants]) -> str:
    if name in objects_seen:
        return ''

    if variants:
        members = [m for m in members
                   if m.name != variants.tag_member.name]

    ret = ''
    objects_seen.add(name)

    if variants:
        ret += gen_rs_variants(name, ifcond, variants)

    ret += mcgen('''

%(cfg)s
#[derive(Clone, Debug, PartialEq)]
pub struct %(rs_name)s {
''',
                 cfg=ifcond.rsgen(),
                 rs_name=rs_name(name))

    if base:
        if not base.is_implicit():
            ret += mcgen('''
    // Members inherited:
''',
                         c_name=base.c_name())
        base_members = base.members
        if variants:
            base_members = [m for m in base.members
                            if m.name != variants.tag_member.name]
        ret += gen_struct_members(base_members, name)
        if not base.is_implicit():
            ret += mcgen('''
    // Own members:
''')

    ret += gen_struct_members(members, name)

    if variants:
        ret += mcgen('''
    pub u: %(rs_type)sVariant,
''', rs_type=rs_name(name))
    ret += mcgen('''
}
''')
    return ret


def gen_rs_enum(name: str,
                ifcond: QAPISchemaIfCond,
                members: List[QAPISchemaEnumMember]) -> str:
    ret = mcgen('''

%(cfg)s
#[derive(Copy, Clone, Debug, PartialEq)]
''',
                cfg=ifcond.rsgen())

    if members:
        ret += '''#[repr(u32)]
#[derive(common::TryInto)]
'''
    ret += mcgen('''
pub enum %(rs_name)s {
''',
                 rs_name=rs_name(name))

    for member in members:
        ret += mcgen('''
    %(cfg)s
    %(c_enum)s,
''',
                     cfg=member.ifcond.rsgen(),
                     c_enum=rs_name(camel_to_upper(member.name)))
    ret += '''}

'''

    # pick the first, since that's what malloc0 does
    if members:
        default = rs_name(camel_to_upper(members[0].name))
        ret += mcgen('''
%(cfg)s
impl Default for %(rs_name)s {
    #[inline]
    fn default() -> %(rs_name)s {
        Self::%(default)s
    }
}
''',
                     cfg=ifcond.rsgen(),
                     rs_name=rs_name(name),
                     default=default)
    return ret


def gen_rs_alternate(name: str,
                     ifcond: QAPISchemaIfCond,
                     variants: QAPISchemaVariants) -> str:
    if name in objects_seen:
        return ''

    ret = ''
    objects_seen.add(name)

    ret += mcgen('''
%(cfg)s
#[derive(Clone, Debug, PartialEq)]
pub enum %(rs_name)s {
''',
                 cfg=ifcond.rsgen(),
                 rs_name=rs_name(name))

    for var in variants.variants:
        if var.type.name == 'q_empty':
            continue
        typ = var.type.rs_type()
        if has_recursive_type(var.type, name, set()):
            typ = 'Box<%s>' % typ
        ret += mcgen('''
        %(cfg)s
        %(mem_name)s(%(rs_type)s),
''',
                     cfg=var.ifcond.rsgen(),
                     rs_type=typ,
                     mem_name=rs_name(to_camel_case(var.name)))

    ret += mcgen('''
}
''')
    return ret


class QAPISchemaGenRsTypeVisitor(QAPISchemaRsVisitor):
    _schema: Optional[QAPISchema]

    def __init__(self, prefix: str) -> None:
        super().__init__(prefix, 'qapi-types',
                         'Schema-defined QAPI types', __doc__)

    def visit_begin(self, schema: QAPISchema) -> None:
        self._schema = schema
        objects_seen.add(schema.the_empty_object_type.name)

        self._gen.preamble_add(
            mcgen('''
#![allow(unexpected_cfgs)]
#![allow(non_camel_case_types)]
#![allow(clippy::empty_structs_with_brackets)]
#![allow(clippy::large_enum_variant)]
#![allow(clippy::pub_underscore_fields)]

// Because QAPI structs can contain float, for simplicity we never
// derive Eq.  Clippy however would complain for those structs
// that *could* be Eq too.
#![allow(clippy::derive_partial_eq_without_eq)]

use util::qobject::QObject;
'''))

    def visit_object_type(self,
                          name: str,
                          info: Optional[QAPISourceInfo],
                          ifcond: QAPISchemaIfCond,
                          features: List[QAPISchemaFeature],
                          base: Optional[QAPISchemaObjectType],
                          members: List[QAPISchemaObjectTypeMember],
                          branches: Optional[QAPISchemaVariants]) -> None:
        assert self._schema is not None
        if self._schema.is_predefined(name) or name.startswith('q_'):
            return
        self._gen.add(gen_rs_object(name, ifcond, base, members, branches))

    def visit_enum_type(self,
                        name: str,
                        info: Optional[QAPISourceInfo],
                        ifcond: QAPISchemaIfCond,
                        features: List[QAPISchemaFeature],
                        members: List[QAPISchemaEnumMember],
                        prefix: Optional[str]) -> None:
        assert self._schema is not None
        if self._schema.is_predefined(name):
            return
        self._gen.add(gen_rs_enum(name, ifcond, members))

    def visit_alternate_type(self,
                             name: str,
                             info: Optional[QAPISourceInfo],
                             ifcond: QAPISchemaIfCond,
                             features: List[QAPISchemaFeature],
                             alternatives: QAPISchemaVariants) -> None:
        self._gen.add(gen_rs_alternate(name, ifcond, alternatives))


def gen_rs_types(schema: QAPISchema, output_dir: str, prefix: str) -> None:
    vis = QAPISchemaGenRsTypeVisitor(prefix)
    schema.visit(vis)
    vis.write(output_dir)
