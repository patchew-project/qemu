# This work is licensed under the terms of the GNU GPL, version 2.
# See the COPYING file in the top-level directory.
"""
QAPI Rust sys/ffi generator
"""

from qapi.common import *
from qapi.rs import *
from qapi.schema import QAPISchemaEnumMember, QAPISchemaObjectType


objects_seen = set()


def gen_rs_sys_enum(name, ifcond, members, prefix=None):
    if ifcond:
        raise NotImplementedError("ifcond are not implemented")
    # append automatically generated _max value
    enum_members = members + [QAPISchemaEnumMember('_MAX', None)]

    ret = mcgen('''

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
#[repr(C)]
pub enum %(rs_name)s {
''',
    rs_name=rs_name(name))

    for m in enum_members:
        if m.ifcond:
            raise NotImplementedError("ifcond are not implemented")
        ret += mcgen('''
    %(c_enum)s,
''',
                     c_enum=to_camel_case(rs_name(m.name, False)))
    ret += mcgen('''
}
''')
    return ret


def gen_rs_sys_struct_members(members):
    ret = ''
    for memb in members:
        if memb.ifcond:
            raise NotImplementedError("ifcond are not implemented")
        if memb.optional:
            ret += mcgen('''
    pub has_%(rs_name)s: bool,
''',
                         rs_name=rs_name(memb.name, protect=False))
        ret += mcgen('''
    pub %(rs_name)s: %(rs_systype)s,
''',
                     rs_systype=rs_systype(memb.type.c_type(), ''), rs_name=rs_name(memb.name))
    return ret


def gen_rs_sys_free(ty):
    return mcgen('''

extern "C" {
    pub fn qapi_free_%(ty)s(obj: *mut %(ty)s);
}
''', ty=ty)


def gen_rs_sys_variants(name, variants):
    ret = mcgen('''

#[repr(C)]
#[derive(Copy, Clone)]
pub union %(rs_name)s { /* union tag is @%(tag_name)s */
''',
                tag_name=rs_name(variants.tag_member.name),
                rs_name=name)

    for var in variants.variants:
        if var.ifcond:
            raise NotImplementedError("ifcond are not implemented")
        if var.type.name == 'q_empty':
            continue
        ret += mcgen('''
    pub %(rs_name)s: %(rs_systype)s,
''',
                     rs_systype=rs_systype(var.type.c_unboxed_type(), ''),
                     rs_name=rs_name(var.name))

    ret += mcgen('''
}

impl ::std::fmt::Debug for %(rs_name)s {
    fn fmt(&self, f: &mut ::std::fmt::Formatter) -> ::std::fmt::Result {
        f.debug_struct(&format!("%(rs_name)s @ {:?}", self as *const _))
            .finish()
    }
}
''', rs_name=name)

    return ret


def gen_rs_sys_object(name, ifcond, base, members, variants):
    if ifcond:
        raise NotImplementedError("ifcond are not implemented")
    if name in objects_seen:
        return ''

    ret = ''
    objects_seen.add(name)
    unionty = name + 'Union'
    if variants:
        for v in variants.variants:
            if v.ifcond:
                raise NotImplementedError("ifcond are not implemented")
            if isinstance(v.type, QAPISchemaObjectType):
                ret += gen_rs_sys_object(v.type.name, v.type.ifcond, v.type.base,
                                         v.type.local_members, v.type.variants)
        ret += gen_rs_sys_variants(unionty, variants)

    ret += gen_rs_sys_free(rs_name(name))
    ret += mcgen('''

#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct %(rs_name)s {
''',
                 rs_name=rs_name(name))

    if base:
        if not base.is_implicit():
            ret += mcgen('''
    // Members inherited:
''')
        ret += gen_rs_sys_struct_members(base.members)
        if not base.is_implicit():
            ret += mcgen('''
    // Own members:
''')

    ret += gen_rs_sys_struct_members(members)
    if variants:
        ret += mcgen('''
        pub u: %(unionty)s
''', unionty=unionty)
    ret += mcgen('''
}
''')
    return ret


def gen_rs_sys_variant(name, ifcond, variants):
    if ifcond:
        raise NotImplementedError("ifcond are not implemented")
    if name in objects_seen:
        return ''

    objects_seen.add(name)

    vs = ''
    for var in variants.variants:
        if var.type.name == 'q_empty':
            continue
        vs += mcgen('''
    pub %(mem_name)s: %(rs_systype)s,
''',
                     rs_systype=rs_systype(var.type.c_unboxed_type(), ''),
                     mem_name=rs_name(var.name))

    return mcgen('''

#[repr(C)]
#[derive(Copy,Clone)]
pub union %(rs_name)sUnion {
    %(variants)s
}

impl ::std::fmt::Debug for %(rs_name)sUnion {
    fn fmt(&self, f: &mut ::std::fmt::Formatter) -> ::std::fmt::Result {
        f.debug_struct(&format!("%(rs_name)sUnion @ {:?}", self as *const _))
            .finish()
    }
}

#[repr(C)]
#[derive(Copy,Clone,Debug)]
pub struct %(rs_name)s {
    pub ty: QType,
    pub u: %(rs_name)sUnion,
}
''',
                 rs_name=rs_name(name), variants=vs)


def gen_rs_sys_array(name, ifcond, element_type):
    if ifcond:
        raise NotImplementedError("ifcond are not implemented")
    ret = mcgen('''

#[repr(C)]
#[derive(Copy,Clone)]
pub struct %(rs_name)s {
    pub next: *mut %(rs_name)s,
    pub value: %(rs_systype)s,
}

impl ::std::fmt::Debug for %(rs_name)s {
    fn fmt(&self, f: &mut ::std::fmt::Formatter) -> ::std::fmt::Result {
        f.debug_struct(&format!("%(rs_name)s @ {:?}", self as *const _))
            .finish()
    }
}
''',
                 rs_name=rs_name(name), rs_systype=rs_systype(element_type.c_type(), ''))
    ret += gen_rs_sys_free(rs_name(name))
    return ret


class QAPISchemaGenRsSysTypeVisitor(QAPISchemaRsVisitor):

    def __init__(self, prefix):
        super().__init__(prefix, 'qapi-sys-types')

    def visit_begin(self, schema):
        # gen_object() is recursive, ensure it doesn't visit the empty type
        objects_seen.add(schema.the_empty_object_type.name)
        self._gen.preamble_add(
            mcgen('''
// generated by qapi-gen, DO NOT EDIT

use common::sys::{QNull, QObject};

'''))

    def visit_enum_type(self, name, info, ifcond, features, members, prefix):
        self._gen.add(gen_rs_sys_enum(name, ifcond, members, prefix))

    def visit_array_type(self, name, info, ifcond, element_type):
        self._gen.add(gen_rs_sys_array(name, ifcond, element_type))

    def visit_object_type(self, name, info, ifcond, features,
                          base, members, variants):
        if name.startswith('q_'):
            return
        self._gen.add(gen_rs_sys_object(name, ifcond, base, members, variants))

    def visit_alternate_type(self, name, info, ifcond, features, variants):
        self._gen.add(gen_rs_sys_variant(name, ifcond, variants))


def gen_rs_systypes(schema, output_dir, prefix, opt_builtins):
    vis = QAPISchemaGenRsSysTypeVisitor(prefix)
    schema.visit(vis)
    vis.write(output_dir)
