# This work is licensed under the terms of the GNU GPL, version 2.
# See the COPYING file in the top-level directory.
"""
QAPI Rust types generator
"""

from typing import List, Optional, Set

from .common import mcgen
from .rs import (
    QAPISchemaRsVisitor,
    rs_name,
    rs_type,
    to_camel_case,
    to_lower_case,
    to_upper_case,
)
from .schema import (
    QAPISchema,
    QAPISchemaAlternateType,
    QAPISchemaArrayType,
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
                tag=rs_type(variants.tag_member.type.c_type(), ''))

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
                     tag_name=rs_name(to_upper_case(tag_name)),
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
        var_name = rs_name(to_camel_case(var.name), False)
        if type_name == 'q_empty':
            ret += mcgen('''
    %(cfg)s
    %(var_name)s,
''',
                         cfg=var.ifcond.rsgen(),
                         var_name=var_name)
        else:
            c_type = var.type.c_unboxed_type()
            if c_type.endswith('_wrapper'):
                c_type = c_type[6:-8]  # remove q_obj*-wrapper
            ret += mcgen('''
    %(cfg)s
    %(var_name)s(%(rs_type)s),
''',
                         cfg=var.ifcond.rsgen(),
                         var_name=var_name,
                         rs_type=rs_type(c_type, ''))

    ret += mcgen('''
}
''')

    ret += gen_rs_variants_to_tag(name, ifcond, variants)

    return ret


def gen_rs_members(members: List[QAPISchemaObjectTypeMember],
                   exclude: Optional[List[str]] = None) -> List[str]:
    exclude = exclude or []
    return [f"{m.ifcond.rsgen()} {rs_name(to_lower_case(m.name))}"
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
    if isinstance(memb, QAPISchemaArrayType):
        return has_recursive_type(memb.element_type, name, visited)
    return False


def gen_struct_members(members: List[QAPISchemaObjectTypeMember],
                       name: str) -> str:
    ret = ''
    for memb in members:
        is_recursive = has_recursive_type(memb.type, name, set())
        typ = rs_type(memb.type.c_type(), '',
                      optional=memb.optional, box=is_recursive)
        ret += mcgen('''
    %(cfg)s
    pub %(rs_name)s: %(rs_type)s,
''',
                     cfg=memb.ifcond.rsgen(),
                     rs_type=typ,
                     rs_name=rs_name(to_lower_case(memb.name)))
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
    # append automatically generated _max value
    enum_members = members + [QAPISchemaEnumMember('_MAX', None)]

    ret = mcgen('''

%(cfg)s
#[repr(u32)]
#[derive(Copy, Clone, Debug, PartialEq, common::TryInto)]
pub enum %(rs_name)s {
''',
                cfg=ifcond.rsgen(),
                rs_name=rs_name(name))

    for member in enum_members:
        ret += mcgen('''
    %(cfg)s
    %(c_enum)s,
''',
                     cfg=member.ifcond.rsgen(),
                     c_enum=rs_name(to_upper_case(member.name)))
    # picked the first, since that's what malloc0 does
    default = rs_name(to_upper_case(enum_members[0].name))
    ret += mcgen('''
}

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
        is_recursive = has_recursive_type(var.type, name, set())
        ret += mcgen('''
        %(cfg)s
        %(mem_name)s(%(rs_type)s),
''',
                     cfg=var.ifcond.rsgen(),
                     rs_type=rs_type(var.type.c_unboxed_type(), '',
                                     box=is_recursive),
                     mem_name=rs_name(to_camel_case(var.name)))

    ret += mcgen('''
}
''')
    return ret


class QAPISchemaGenRsTypeVisitor(QAPISchemaRsVisitor):

    def __init__(self, prefix: str) -> None:
        super().__init__(prefix, 'qapi-types')

    def visit_begin(self, schema: QAPISchema) -> None:
        # don't visit the empty type
        objects_seen.add(schema.the_empty_object_type.name)
        self._gen.preamble_add(
            mcgen('''
// @generated by qapi-gen, DO NOT EDIT

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
        if name.startswith('q_'):
            return
        self._gen.add(gen_rs_object(name, ifcond, base, members, branches))

    def visit_enum_type(self,
                        name: str,
                        info: Optional[QAPISourceInfo],
                        ifcond: QAPISchemaIfCond,
                        features: List[QAPISchemaFeature],
                        members: List[QAPISchemaEnumMember],
                        prefix: Optional[str]) -> None:
        self._gen.add(gen_rs_enum(name, ifcond, members))

    def visit_alternate_type(self,
                             name: str,
                             info: Optional[QAPISourceInfo],
                             ifcond: QAPISchemaIfCond,
                             features: List[QAPISchemaFeature],
                             alternatives: QAPISchemaVariants) -> None:
        self._gen.add(gen_rs_alternate(name, ifcond, alternatives))


def gen_rs_types(schema: QAPISchema, output_dir: str, prefix: str,
                 builtins: bool) -> None:
    # pylint: disable=unused-argument
    # TODO: builtins?
    vis = QAPISchemaGenRsTypeVisitor(prefix)
    schema.visit(vis)
    vis.write(output_dir)
