# This work is licensed under the terms of the GNU GPL, version 2.
# See the COPYING file in the top-level directory.
"""
QAPI Rust sys/ffi generator
"""

from pathlib import Path
from typing import (
    Dict,
    List,
    Optional,
    Set,
)

from .cabi import CABI, CABIEnum, gen_object_cabi
from .common import mcgen
from .rs import (
    QAPISchemaRsVisitor,
    rs_ffitype,
    rs_name,
    to_camel_case,
    to_snake_case,
)
from .schema import (
    QAPISchema,
    QAPISchemaEnumMember,
    QAPISchemaFeature,
    QAPISchemaIfCond,
    QAPISchemaModule,
    QAPISchemaObjectType,
    QAPISchemaObjectTypeMember,
    QAPISchemaType,
    QAPISchemaVariants,
)
from .source import QAPISourceInfo


objects_seen = set()


def gen_rs_ffi_enum(name: str,
                    ifcond: QAPISchemaIfCond,
                    members: List[QAPISchemaEnumMember]) -> str:
    # append automatically generated _max value
    enum_members = members + [QAPISchemaEnumMember('_MAX', None)]

    ret = mcgen('''

%(cfg)s
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
#[repr(C)]
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
                     c_enum=to_camel_case(rs_name(member.name)))
    # picked the first, since that's what malloc0 does
    # but arguably could use _MAX instead, or a qapi annotation
    default = to_camel_case(rs_name(enum_members[0].name))
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


def gen_rs_ffi_struct_members(members:
                              List[QAPISchemaObjectTypeMember]) -> str:
    ret = ''
    for memb in members:
        if memb.optional:
            ret += mcgen('''
    %(cfg)s
    pub has_%(rs_name)s: bool,
''',
                         cfg=memb.ifcond.rsgen(),
                         rs_name=to_snake_case(rs_name(memb.name,
                                                       protect=False)))
        ret += mcgen('''
    %(cfg)s
    pub %(rs_name)s: %(rs_ffitype)s,
''',
                     cfg=memb.ifcond.rsgen(),
                     rs_ffitype=rs_ffitype(memb.type.c_type(), ''),
                     rs_name=to_snake_case(rs_name(memb.name)))
    return ret


def gen_rs_ffi_free(name: str, ifcond: QAPISchemaIfCond) -> str:
    name = rs_name(name, protect=False)
    typ = rs_name(name)
    return mcgen('''

%(cfg)s
extern "C" {
    pub fn qapi_free_%(name)s(obj: *mut %(ty)s);
}
''', cfg=ifcond.rsgen(), name=name, ty=typ)


def gen_rs_ffi_variants(name: str,
                        ifcond: QAPISchemaIfCond,
                        variants: Optional[QAPISchemaVariants]) -> str:
    ret = mcgen('''

%(cfg)s
#[repr(C)]
#[derive(Copy, Clone)]
pub union %(rs_name)s { /* union tag is @%(tag_name)s */
''',
                cfg=ifcond.rsgen(),
                tag_name=rs_name(variants.tag_member.name),
                rs_name=rs_name(name))

    empty = True
    for var in variants.variants:
        if var.type.name == 'q_empty':
            continue
        empty = False
        ret += mcgen('''
    %(cfg)s
    pub %(rs_name)s: %(rs_ffitype)s,
''',
                     cfg=var.ifcond.rsgen(),
                     rs_ffitype=rs_ffitype(var.type.c_unboxed_type(), ''),
                     rs_name=rs_name(var.name))

    ret += mcgen('''
    pub qapi_dummy: QapiDummy,
''')

    ret += mcgen('''
}

%(cfg)s
impl ::std::fmt::Debug for %(rs_name)s {
    fn fmt(&self, f: &mut ::std::fmt::Formatter) -> ::std::fmt::Result {
        f.debug_struct(&format!("%(rs_name)s @ {:?}", self as *const _))
            .finish()
    }
}
''', cfg=ifcond.rsgen(), rs_name=rs_name(name))

    if empty:
        ret += mcgen('''

%(cfg)s
impl ::std::default::Default for %(rs_name)s {
    fn default() -> Self {
        Self { qapi_dummy: QapiDummy }
    }
}
''', cfg=ifcond.rsgen(), rs_name=rs_name(name))
    return ret


def gen_rs_ffi_object(name: str,
                      ifcond: QAPISchemaIfCond,
                      base: Optional[QAPISchemaObjectType],
                      members: List[QAPISchemaObjectTypeMember],
                      variants: Optional[QAPISchemaVariants]) -> str:
    if name in objects_seen:
        return ''

    ret = ''
    objects_seen.add(name)
    unionty = name + 'Union'
    if variants:
        for var in variants.variants:
            if isinstance(var.type, QAPISchemaObjectType):
                ret += gen_rs_ffi_object(var.type.name,
                                         var.type.ifcond,
                                         var.type.base,
                                         var.type.local_members,
                                         var.type.variants)
        ret += gen_rs_ffi_variants(unionty, ifcond, variants)

    ret += gen_rs_ffi_free(name, ifcond)
    ret += mcgen('''

%(cfg)s
#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct %(rs_name)s {
''',
                 cfg=ifcond.rsgen(),
                 rs_name=rs_name(name))

    if base:
        if not base.is_implicit():
            ret += mcgen('''
    // Members inherited:
''')
        ret += gen_rs_ffi_struct_members(base.members)
        if not base.is_implicit():
            ret += mcgen('''
    // Own members:
''')

    ret += gen_rs_ffi_struct_members(members)
    if variants:
        ret += mcgen('''
        pub u: %(unionty)s
''', unionty=rs_name(unionty))

    empty = False
    # for compatibility with C ABI
    if (not base or base.is_empty()) and not members and not variants:
        empty = True
        ret += mcgen('''
    pub qapi_dummy_for_empty_struct: u8,
''')
    ret += mcgen('''
}
''')

    if empty:
        ret += mcgen('''

%(cfg)s
impl Default for %(rs_name)s {
    fn default() -> Self {
        Self { qapi_dummy_for_empty_struct: 0 }
    }
}
''',
                     cfg=ifcond.rsgen(),
                     rs_name=rs_name(name))

    return ret


def gen_rs_ffi_variant(name: str,
                       ifcond: QAPISchemaIfCond,
                       variants: Optional[QAPISchemaVariants]) -> str:
    if name in objects_seen:
        return ''

    objects_seen.add(name)

    ret = ''
    gen_variants = ''
    for var in variants.variants:
        if var.type.name == 'q_empty':
            continue
        typ = rs_ffitype(var.type.c_unboxed_type(), '')
        gen_variants += mcgen('''
    %(cfg)s
    pub %(mem_name)s: %(rs_ffitype)s,
''',
                              cfg=var.ifcond.rsgen(),
                              rs_ffitype=typ,
                              mem_name=rs_name(var.name))
    if not gen_variants:
        ret += mcgen('''
   impl ::std::default::Default for %(rs_name)sUnion {
       fn default() -> Self {
           Self { qapi_dummy: QapiDummy }
       }
   }
''')
    gen_variants += mcgen('''
    pub qapi_dummy: QapiDummy,
''')

    ret += gen_rs_ffi_free(name, ifcond)
    ret += mcgen('''

%(cfg)s
#[repr(C)]
#[derive(Copy,Clone)]
pub union %(rs_name)sUnion {
    %(variants)s
}

%(cfg)s
impl ::std::fmt::Debug for %(rs_name)sUnion {
    fn fmt(&self, f: &mut ::std::fmt::Formatter) -> ::std::fmt::Result {
        f.debug_struct(&format!("%(rs_name)sUnion @ {:?}", self as *const _))
            .finish()
    }
}

%(cfg)s
#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct %(rs_name)s {
    pub %(tag)s: QType,
    pub u: %(rs_name)sUnion,
}
''',
                 cfg=ifcond.rsgen(),
                 rs_name=rs_name(name),
                 tag=rs_name(variants.tag_member.name),
                 variants=gen_variants)
    return ret


def gen_rs_ffi_array(name: str,
                     ifcond: QAPISchemaIfCond,
                     element_type: QAPISchemaType) -> str:
    ret = mcgen('''

%(cfg)s
#[repr(C)]
#[derive(Copy, Clone)]
pub struct %(rs_name)s {
    pub next: *mut %(rs_name)s,
    pub value: %(rs_ffitype)s,
}

%(cfg)s
impl ::std::fmt::Debug for %(rs_name)s {
    fn fmt(&self, f: &mut ::std::fmt::Formatter) -> ::std::fmt::Result {
        f.debug_struct(&format!("%(rs_name)s @ {:?}", self as *const _))
            .finish()
    }
}
''',
                cfg=ifcond.rsgen(),
                rs_name=rs_name(name),
                rs_ffitype=rs_ffitype(element_type.c_type(), ''))
    ret += gen_rs_ffi_free(name, ifcond)
    return ret


class QAPISchemaGenRsFFITypeVisitor(QAPISchemaRsVisitor):

    def __init__(self, prefix: str):
        super().__init__(prefix, 'qapi-ffi-types')
        self._cabi: Dict[str, CABI] = {}
        self._cabi_functions: List[str] = []
        self._cabi_functions_called: Set[str] = set()

    def _cabi_add(self, cabis: List[CABI]) -> None:
        for cabi in cabis:
            self._cabi.setdefault(cabi.name, cabi)

    def visit_begin(self, schema: QAPISchema) -> None:
        # gen_object() is recursive, ensure it doesn't visit the empty type
        objects_seen.add(schema.the_empty_object_type.name)
        self._gen.preamble_add(
            mcgen('''
// generated by qapi-gen, DO NOT EDIT

#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct QapiDummy;
'''))

    def _get_qapi_cabi_fn(self, name: str) -> str:
        fn_name = 'cabi'
        if QAPISchemaModule.is_builtin_module(name):
            fn_name += '_builtin'
        elif name != self._main_module:
            name = Path(name).stem
            fn_name += '_' + rs_name(name, False)
        return fn_name

    def visit_include(self, name: str, info: Optional[QAPISourceInfo]) -> None:
        super().visit_include(name, info)
        cabi_fn = self._get_qapi_cabi_fn(name)
        if cabi_fn not in self._cabi_functions_called:
            self._cabi_functions.append(cabi_fn)

    def visit_module_end(self, name: str) -> None:
        cabi_gen = "".join(f'    {fn}();\n' for fn in self._cabi_functions)
        self._cabi_functions_called |= set(self._cabi_functions)
        self._cabi_functions = []
        cabi_gen += "".join(
            [c.gen_rs() for _, c in sorted(self._cabi.items())]
        )
        self._cabi = {}
        fn_name = self._get_qapi_cabi_fn(name)
        self._gen.add(mcgen('''
#[cfg(QAPI_CABI)]
pub(crate) fn %(fn_name)s() {
%(cabi_gen)s
}
''', fn_name=fn_name, cabi_gen=cabi_gen))

    def visit_enum_type(self,
                        name: str,
                        info: Optional[QAPISourceInfo],
                        ifcond: QAPISchemaIfCond,
                        features: List[QAPISchemaFeature],
                        members: List[QAPISchemaEnumMember],
                        prefix: Optional[str]) -> None:
        self._gen.add(gen_rs_ffi_enum(name, ifcond, members))
        self._cabi_add([CABIEnum(name, ifcond, members, prefix)])

    def visit_array_type(self,
                         name: str,
                         info: Optional[QAPISourceInfo],
                         ifcond: QAPISchemaIfCond,
                         element_type: QAPISchemaType) -> None:
        self._gen.add(gen_rs_ffi_array(name, ifcond, element_type))

    def visit_object_type(self,
                          name: str,
                          info: Optional[QAPISourceInfo],
                          ifcond: QAPISchemaIfCond,
                          features: List[QAPISchemaFeature],
                          base: Optional[QAPISchemaObjectType],
                          members: List[QAPISchemaObjectTypeMember],
                          variants: Optional[QAPISchemaVariants]) -> None:
        # Nothing to do for the special empty builtin
        if name == 'q_empty':
            return
        self._gen.add(gen_rs_ffi_object(name, ifcond, base, members, variants))
        self._cabi_add(gen_object_cabi(name, ifcond, base, members, variants))

    def visit_alternate_type(self,
                             name: str,
                             info: QAPISourceInfo,
                             ifcond: QAPISchemaIfCond,
                             features: List[QAPISchemaFeature],
                             variants: QAPISchemaVariants) -> None:
        self._gen.add(gen_rs_ffi_variant(name, ifcond, variants))
        self._cabi_add(gen_object_cabi(name, ifcond, None,
                                       [variants.tag_member], variants))


def gen_rs_ffitypes(schema: QAPISchema,
                    output_dir: str,
                    prefix: str) -> None:
    vis = QAPISchemaGenRsFFITypeVisitor(prefix)
    schema.visit(vis)
    vis.write(output_dir)
