# This work is licensed under the terms of the GNU GPL, version 2.
# See the COPYING file in the top-level directory.
"""
QAPI Rust types generator
"""

from typing import List, Optional

from .common import POINTER_SUFFIX, mcgen
from .rs import (
    QAPISchemaRsVisitor,
    from_qemu,
    rs_ctype_parse,
    rs_ffitype,
    rs_name,
    rs_type,
    to_camel_case,
    to_snake_case,
)
from .schema import (
    QAPISchema,
    QAPISchemaEnumMember,
    QAPISchemaEnumType,
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
                           variants: Optional[QAPISchemaVariants]) -> str:
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
        var_name = to_camel_case(rs_name(var.name))
        patt = '(_)'
        if type_name == 'q_empty':
            patt = ''
        ret += mcgen('''
    %(cfg)s
    %(rs_name)sVariant::%(var_name)s%(patt)s => Self::%(var_name)s,
''',
                     cfg=var.ifcond.rsgen(),
                     rs_name=rs_name(name),
                     var_name=var_name,
                     patt=patt)

    ret += mcgen('''
        }
    }
}
''')
    return ret


def variants_to_qemu_inner(name: str,
                           variants: Optional[QAPISchemaVariants]) -> str:
    members = ''
    none_arms = ''
    full_arms = ''
    lifetime = ''
    for var in variants.variants:
        var_name = to_camel_case(rs_name(var.name))
        type_name = var.type.name
        if type_name == 'q_empty':
            members += mcgen('''
    %(cfg)s
    %(var_name)s,
''',
                             cfg=var.ifcond.rsgen(),
                             var_name=var_name)
            none_arms += mcgen('''
    %(cfg)s
    %(rs_name)sVariant::%(var_name)s => {
        (std::ptr::null_mut(),
         %(rs_name)sVariantStorage::%(var_name)s)
    },
''',
                               cfg=var.ifcond.rsgen(),
                               rs_name=rs_name(name),
                               var_name=var_name)
            full_arms += mcgen('''
    %(cfg)s
    %(rs_name)sVariant::%(var_name)s => {
        std::ptr::null_mut()
    }
''',
                               cfg=var.ifcond.rsgen(),
                               rs_name=rs_name(name),
                               var_name=var_name)
            continue
        c_type = var.type.c_unboxed_type()
        if type_name.endswith('-wrapper'):
            wrap = list(var.type.members)[0]
            type_name = wrap.type.name
            c_type = wrap.type.c_unboxed_type()

        lifetime = "<'a>"
        (_, _, is_list, ffitype) = rs_ctype_parse(c_type)
        ffitype = rs_ffitype(ffitype)
        ptr_ty = 'NewPtr<*mut %s>' % ffitype if is_list else '*mut ' + ffitype
        stash_ty = ': Stash<%s, _>' % ptr_ty if is_list else ''

        members += mcgen('''
    %(cfg)s
    %(var_name)s(<%(rs_type)s as ToQemuPtr<'a, %(ptr_ty)s>>::Storage),
''',
                         cfg=var.ifcond.rsgen(),
                         var_name=var_name,
                         rs_type=rs_type(c_type, ''),
                         ptr_ty=ptr_ty)
        none_arms += mcgen('''
    %(cfg)s
    %(rs_name)sVariant::%(var_name)s(v) => {
        let stash_%(stash_ty)s = v.to_qemu_none();
        (stash_.0.to() as *mut std::ffi::c_void,
         %(rs_name)sVariantStorage::%(var_name)s(stash_.1))
    },
''',
                           cfg=var.ifcond.rsgen(),
                           rs_name=rs_name(name),
                           var_name=var_name,
                           stash_ty=stash_ty)
        ptr_ty = ': %s' % ptr_ty if is_list else ''
        full_arms += mcgen('''
    %(cfg)s
    %(rs_name)sVariant::%(var_name)s(v) => {
        let ptr%(ptr_ty)s = v.to_qemu_full();
        ptr.to() as *mut std::ffi::c_void
    },
''',
                           cfg=var.ifcond.rsgen(),
                           rs_name=rs_name(name),
                           var_name=var_name,
                           ptr_ty=ptr_ty)
    return (members, none_arms, full_arms, lifetime)


def gen_rs_variants_to_qemu(name: str,
                            ifcond: QAPISchemaIfCond,
                            variants: Optional[QAPISchemaVariants]) -> str:
    (members, none_arms, full_arms, lifetime) = \
        variants_to_qemu_inner(name, variants)
    return mcgen('''

%(cfg)s
impl QemuPtrDefault for %(rs_name)sVariant {
    type QemuType = *mut std::ffi::c_void;
}

%(cfg)s
pub enum %(rs_name)sVariantStorage%(lt)s {
    %(members)s
}

%(cfg)s
impl<'a> ToQemuPtr<'a, *mut std::ffi::c_void> for %(rs_name)sVariant {
    type Storage = %(rs_name)sVariantStorage%(lt)s;

    #[inline]
    fn to_qemu_none(&'a self)
    -> Stash<'a, *mut std::ffi::c_void, %(rs_name)sVariant> {
        let (ptr_, cenum_) = match self {
             %(none_arms)s
        };

        Stash(ptr_, cenum_)
    }

    #[inline]
    fn to_qemu_full(&self) -> *mut std::ffi::c_void {
        match self {
            %(full_arms)s
        }
    }
}
''',
                 cfg=ifcond.rsgen(),
                 rs_name=rs_name(name),
                 lt=lifetime,
                 members=members,
                 none_arms=none_arms,
                 full_arms=full_arms)


def gen_rs_variants(name: str,
                    ifcond: QAPISchemaIfCond,
                    variants: Optional[QAPISchemaVariants]) -> str:
    ret = mcgen('''

%(cfg)s
#[derive(Clone,Debug)]
pub enum %(rs_name)sVariant {
''',
                cfg=ifcond.rsgen(),
                rs_name=rs_name(name))

    for var in variants.variants:
        type_name = var.type.name
        var_name = to_camel_case(rs_name(var.name, False))
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
    # implement ToQemu trait for the storage handling
    # no need for gen_rs_variants_from_qemu() however
    ret += gen_rs_variants_to_qemu(name, ifcond, variants)

    return ret


def gen_rs_object_to_qemu(name: str,
                          ifcond: QAPISchemaIfCond,
                          base: Optional[QAPISchemaObjectType],
                          members: List[QAPISchemaObjectTypeMember],
                          variants: Optional[QAPISchemaVariants]) -> str:
    storage = []
    stash = []
    ffi_memb = []
    memb_none = ''
    memb_full = ''
    if base:
        members = list(base.members) + members
    for memb in members:
        if variants and variants.tag_member.name == memb.name:
            continue
        memb_name = to_snake_case(rs_name(memb.name))
        c_type = memb.type.c_type()
        (is_pointer, _, is_list, _) = rs_ctype_parse(c_type)
        if is_pointer:
            if memb.ifcond.is_present():
                raise NotImplementedError("missing support for condition here")
            typ = rs_type(memb.type.c_type(),
                          optional=memb.optional,
                          qapi_ns='',
                          box=True)
            styp = rs_ffitype(memb.type.c_type(), list_as_newp=True)
            storage.append("Stash<'a, %s, %s>" % (styp, typ))
        if memb.optional:
            has_memb_name = 'has_%s' % rs_name(memb.name, protect=False)
            ffi_memb.append(f"{memb.ifcond.rsgen()} {has_memb_name}")
            has_memb = mcgen('''
    %(cfg)s
    let %(has_memb_name)s = self.%(memb_name)s.is_some();
''',
                             cfg=memb.ifcond.rsgen(),
                             memb_name=memb_name,
                             has_memb_name=has_memb_name)
            memb_none += has_memb
            memb_full += has_memb

        if is_pointer:
            stash_name = '{}_stash_'.format(memb_name)
            stash.append(stash_name)
            var = 'NewPtr(%s)' % memb_name if is_list else memb_name
            memb_none += mcgen('''
    let %(stash_name)s = self.%(memb_name)s.to_qemu_none();
    let %(var)s = %(stash_name)s.0;
''', stash_name=stash_name, memb_name=memb_name, var=var)
            memb_full += mcgen('''
    let %(var)s = self.%(memb_name)s.to_qemu_full();
''', memb_name=memb_name, var=var)
        else:
            unwrap = ''
            if memb.optional:
                unwrap = '.unwrap_or_default()'
            assign = mcgen('''
    %(cfg)s
    let %(memb_name)s = self.%(memb_name)s%(unwrap)s;
''',
                           cfg=memb.ifcond.rsgen(),
                           memb_name=memb_name,
                           unwrap=unwrap)
            memb_none += assign
            memb_full += assign

        ffi_memb.append(f"{memb.ifcond.rsgen()} {memb_name}")

    if variants:
        tag_name = rs_name(variants.tag_member.name)
        ffi_memb.append(tag_name)
        ffi_memb.append('u')
        voidp = '*mut std::ffi::c_void'
        storage.append("Stash<'a, %s, %sVariant>" % (voidp, rs_name(name)))
        tag = mcgen('''
    let %(tag_name)s = (&self.u).into();
''', tag_name=tag_name)
        memb_none += tag
        memb_full += tag
        arms_none = ''
        arms_full = ''
        for variant in variants.variants:
            typ = variant.type
            if typ.name == 'q_empty':
                arms_none += mcgen('''
    %(cfg)s
    %(rs_name)sVariantStorage::%(kind)s => qapi_ffi::%(rs_name)sUnion {
        qapi_dummy: qapi_ffi::QapiDummy,
    },''',
                                   cfg=variant.ifcond.rsgen(),
                                   rs_name=rs_name(name),
                                   kind=to_camel_case(rs_name(variant.name)))
                arms_full += mcgen('''
    %(cfg)s
    %(rs_name)sVariant::%(kind)s => qapi_ffi::%(rs_name)sUnion {
        qapi_dummy: qapi_ffi::QapiDummy,
    },''',
                                   cfg=variant.ifcond.rsgen(),
                                   rs_name=rs_name(name),
                                   kind=to_camel_case(rs_name(variant.name)))
            else:
                if typ.name.endswith('-wrapper'):
                    wrap_ty = list(typ.members)[0].type.c_type()
                    ptr = wrap_ty.endswith(POINTER_SUFFIX)
                    val = (
                        rs_ffitype(variant.type.c_unboxed_type()) +
                        ' { data: u_stash_.0.to() as *mut _ }' if ptr else
                        ' { data: unsafe { *(u_stash_.0.to() as *const _) } }'
                    )
                else:
                    val = '*_s.0'
                arms_none += mcgen('''
    %(cfg)s
    %(rs_name)sVariantStorage::%(kind)s(ref _s) => qapi_ffi::%(rs_name)sUnion {
        %(var_name)s: %(val)s,
    },''',
                                   cfg=variant.ifcond.rsgen(),
                                   rs_name=rs_name(name),
                                   kind=to_camel_case(rs_name(variant.name)),
                                   var_name=rs_name(variant.name),
                                   val=val)
                arms_full += mcgen('''
    %(cfg)s
    %(rs_name)sVariant::%(kind)s(_) => qapi_ffi::%(rs_name)sUnion {
        %(var_name)s: *(u_ptr_.to() as *const _),
    },''',
                                   cfg=variant.ifcond.rsgen(),
                                   rs_name=rs_name(name),
                                   kind=to_camel_case(rs_name(variant.name)),
                                   var_name=rs_name(variant.name))
        memb_none += mcgen('''
    let u_stash_ = self.u.to_qemu_none();
    let u = match u_stash_.1 {
        %(arms)s
    };
''', arms=arms_none)
        stash.append('u_stash_')
        memb_full += mcgen('''
    let u_ptr_ = self.u.to_qemu_full();
    let u = match self.u {
        %(arms)s
    };
    ffi::g_free(u_ptr_);
''', arms=arms_full)

    if not ffi_memb:
        ffi_memb = ['qapi_dummy_for_empty_struct: 0']

    return mcgen('''

%(cfg)s
impl QemuPtrDefault for %(rs_name)s {
    type QemuType = *mut qapi_ffi::%(rs_name)s;
}

%(cfg)s
impl<'a> ToQemuPtr<'a, *mut qapi_ffi::%(rs_name)s> for %(rs_name)s {
    type Storage = (Box<qapi_ffi::%(rs_name)s>, %(storage)s);

    #[inline]
    fn to_qemu_none(&'a self)
    -> Stash<'a, *mut qapi_ffi::%(rs_name)s, %(rs_name)s> {
        %(memb_none)s
        let mut box_ = Box::new(qapi_ffi::%(rs_name)s { %(ffi_memb)s });

        Stash(&mut *box_, (box_, %(stash)s))
    }

    #[inline]
    fn to_qemu_full(&self) -> *mut qapi_ffi::%(rs_name)s {
        unsafe {
            %(memb_full)s
            let ptr = ffi::g_malloc0(
                std::mem::size_of::<%(rs_name)s>()) as *mut _;
            *ptr = qapi_ffi::%(rs_name)s { %(ffi_memb)s };
            ptr
        }
    }
}
''',
                 cfg=ifcond.rsgen(),
                 rs_name=rs_name(name),
                 storage=', '.join(storage),
                 ffi_memb=', '.join(ffi_memb),
                 memb_none=memb_none,
                 memb_full=memb_full,
                 stash=', '.join(stash))


def gen_rs_members(members: List[QAPISchemaObjectTypeMember],
                   exclude: List[str] = None) -> str:
    exclude = exclude or []
    return [f"{m.ifcond.rsgen()} {to_snake_case(rs_name(m.name))}"
            for m in members if m.name not in exclude]


def gen_rs_object_from_qemu(name: str,
                            ifcond: QAPISchemaIfCond,
                            base: Optional[QAPISchemaObjectType],
                            members: List[QAPISchemaObjectTypeMember],
                            variants: Optional[QAPISchemaVariants]) -> str:
    exclude = [variants.tag_member.name] if variants else []
    memb_names = []
    if base:
        names = gen_rs_members(base.members, exclude)
        memb_names.extend(names)
    names = gen_rs_members(members, exclude)
    memb_names.extend(names)

    ret = mcgen('''

%(cfg)s
impl FromQemuPtrFull<*mut qapi_ffi::%(rs_name)s> for %(rs_name)s {
    unsafe fn from_qemu_full(ffi: *mut qapi_ffi::%(rs_name)s) -> Self {
        let ret = from_qemu_none(ffi as *const _);
        qapi_ffi::qapi_free_%(name)s(ffi);
        ret
    }
}

%(cfg)s
impl FromQemuPtrNone<*const qapi_ffi::%(rs_name)s> for %(rs_name)s {
    unsafe fn from_qemu_none(ffi: *const qapi_ffi::%(rs_name)s) -> Self {
        let _ffi = &*ffi;
''',
                cfg=ifcond.rsgen(),
                name=rs_name(name, protect=False),
                rs_name=rs_name(name))

    if base:
        members = list(base.members) + members

    tag_member = variants.tag_member if variants else None
    for memb in members:
        if memb == tag_member:
            continue
        memb_name = rs_name(memb.name)
        val = from_qemu('_ffi.' + to_snake_case(memb_name), memb.type.c_type())
        if memb.optional:
            val = mcgen('''{
            if _ffi.has_%(memb_name)s {
                Some(%(val)s)
            } else {
                None
            }
}''',
                        memb_name=rs_name(memb.name, protect=False),
                        val=val)

        ret += mcgen('''
        %(cfg)s
        let %(snake_memb_name)s = %(val)s;
''',
                     cfg=memb.ifcond.rsgen(),
                     snake_memb_name=to_snake_case(memb_name),
                     memb_name=memb_name,
                     val=val)

    if variants:
        arms = ''
        assert isinstance(variants.tag_member.type, QAPISchemaEnumType)
        for variant in variants.variants:
            typ = variant.type
            if typ.name == 'q_empty':
                memb = ''
            else:
                ptr = True
                is_list = False
                memb = to_snake_case(rs_name(variant.name))
                if typ.name.endswith('-wrapper'):
                    memb = '_ffi.u.%s.data' % memb
                    wrap_ty = list(typ.members)[0].type.c_type()
                    (ptr, _, is_list, _) = rs_ctype_parse(wrap_ty)
                else:
                    memb = '&_ffi.u.%s' % memb
                if ptr:
                    memb = '%s as *const _' % memb
                    if is_list:
                        memb = 'NewPtr(%s)' % memb
                    memb = 'from_qemu_none(%s)' % memb
                memb = '(%s)' % memb
            arms += mcgen('''
%(cfg)s
%(enum)s::%(variant)s => { %(rs_name)sVariant::%(variant)s%(memb)s },
''',
                          cfg=variant.ifcond.rsgen(),
                          enum=rs_name(variants.tag_member.type.name),
                          memb=memb,
                          variant=to_camel_case(rs_name(variant.name)),
                          rs_name=rs_name(name))
        ret += mcgen('''
        let u = match _ffi.%(tag)s {
            %(arms)s
            _ => panic!("Variant with invalid tag"),
        };
''',
                     tag=rs_name(variants.tag_member.name),
                     arms=arms)
        memb_names.append('u')

    ret += mcgen('''
            Self { %(memb_names)s }
        }
}
''',
                 memb_names=', '.join(memb_names))
    return ret


def gen_struct_members(members: List[QAPISchemaObjectTypeMember]) -> str:
    ret = ''
    for memb in members:
        typ = rs_type(memb.type.c_type(), '', optional=memb.optional, box=True)
        ret += mcgen('''
    %(cfg)s
    pub %(rs_name)s: %(rs_type)s,
''',
                     cfg=memb.ifcond.rsgen(),
                     rs_type=typ,
                     rs_name=to_snake_case(rs_name(memb.name)))
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
#[derive(Clone, Debug)]
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
        ret += gen_struct_members(base_members)
        if not base.is_implicit():
            ret += mcgen('''
    // Own members:
''')

    ret += gen_struct_members(members)

    if variants:
        ret += mcgen('''
    pub u: %(rs_type)sVariant,
''', rs_type=rs_name(name))
    ret += mcgen('''
}
''')

    ret += gen_rs_object_from_qemu(name, ifcond, base, members, variants)
    ret += gen_rs_object_to_qemu(name, ifcond, base, members, variants)
    return ret


def gen_rs_alternate_from_qemu(name: str,
                               ifcond: QAPISchemaIfCond,
                               variants: Optional[QAPISchemaVariants]) -> str:
    arms = ''
    for var in variants.variants:
        qtype = to_camel_case(var.type.alternate_qtype()[6:].lower())
        ptr = var.type.c_unboxed_type().endswith(POINTER_SUFFIX)
        memb = 'ffi.u.%s' % rs_name(var.name)
        if not ptr:
            memb = '&' + memb
        arms += mcgen('''
        %(cfg)s
        QType::%(qtype)s => {
            Self::%(kind)s(from_qemu_none(%(memb)s as *const _))
        }
''',
                      qtype=qtype,
                      cfg=var.ifcond.rsgen(),
                      kind=to_camel_case(rs_name(var.name)),
                      memb=memb)

    ret = mcgen('''

%(cfg)s
impl FromQemuPtrFull<*mut qapi_ffi::%(rs_name)s> for %(rs_name)s {
    unsafe fn from_qemu_full(ffi: *mut qapi_ffi::%(rs_name)s) -> Self {
        let ret = from_qemu_none(ffi as *const _);
        qapi_ffi::qapi_free_%(name)s(ffi);
        ret
    }
}

%(cfg)s
impl FromQemuPtrNone<*const qapi_ffi::%(rs_name)s> for %(rs_name)s {
    unsafe fn from_qemu_none(ffi: *const qapi_ffi::%(rs_name)s) -> Self {
        let ffi = &*ffi;

        match ffi.r#type {
            %(arms)s
            _ => panic!()
        }
    }
}
''',
                cfg=ifcond.rsgen(),
                name=rs_name(name, protect=False),
                rs_name=rs_name(name),
                arms=arms)
    return ret


def gen_rs_alternate_to_qemu(name: str,
                             ifcond: QAPISchemaIfCond,
                             variants: Optional[QAPISchemaVariants],
                             lifetime: str) -> str:
    arms_none = ''
    arms_full = ''
    for var in variants.variants:
        if var.type.name == 'q_empty':
            continue
        ptr = var.type.c_unboxed_type().endswith(POINTER_SUFFIX)
        val = 'val.0' if ptr else 'unsafe { *(val.0.to() as *const _) }'
        stor = '(val.1)' if var.type.c_type().endswith(POINTER_SUFFIX) else ''
        qtype = var.type.alternate_qtype()[6:].lower()
        arms_none += mcgen('''
        %(cfg)s
        Self::%(memb_name)s(val) => {
            let val = val.to_qemu_none();
            (
                QType::%(qtype)s,
                qapi_ffi::%(rs_name)sUnion { %(ffi_memb_name)s: %(val)s },
                %(rs_name)sStorage::%(memb_name)s%(stor)s
            )
        }
''',
                           rs_name=rs_name(name),
                           cfg=var.ifcond.rsgen(),
                           memb_name=to_camel_case(rs_name(var.name)),
                           ffi_memb_name=rs_name(var.name),
                           qtype=to_camel_case(qtype),
                           val=val,
                           stor=stor)
        val = 'val' if ptr else '*val'
        free = '' if ptr else 'ffi::g_free(val as *mut _);'
        arms_full += mcgen('''
        %(cfg)s
        Self::%(memb_name)s(val) => {
            let val = val.to_qemu_full();
            let ret = (QType::%(qtype)s, qapi_ffi::%(rs_name)sUnion {
                %(ffi_memb_name)s: %(val)s
            } );
            %(free)s
            ret
        }
''',
                           rs_name=rs_name(name),
                           cfg=var.ifcond.rsgen(),
                           memb_name=to_camel_case(rs_name(var.name)),
                           ffi_memb_name=rs_name(var.name),
                           qtype=to_camel_case(qtype),
                           val=val,
                           free=free)

    memb_none = mcgen('''
    let (r#type, u, stor) = match self {
        %(arms_none)s
    };
''', arms_none=arms_none)
    memb_full = mcgen('''
    let (r#type, u) = match self {
        %(arms_full)s
    };
''', arms_full=arms_full)
    ffi_memb = ['r#type', 'u']
    return mcgen('''

%(cfg)s
impl QemuPtrDefault for %(rs_name)s {
    type QemuType = *mut qapi_ffi::%(rs_name)s;
}

%(cfg)s
impl<'a> ToQemuPtr<'a, *mut qapi_ffi::%(rs_name)s> for %(rs_name)s {
    // Additional boxing of storage needed due to recursive types
    type Storage = (Box<qapi_ffi::%(rs_name)s>, Box<%(rs_name)sStorage%(lt)s>);

    #[inline]
    fn to_qemu_none(&'a self)
    -> Stash<'a, *mut qapi_ffi::%(rs_name)s, %(rs_name)s> {
        %(memb_none)s
        let mut box_ = Box::new(qapi_ffi::%(rs_name)s { %(ffi_memb)s });

        Stash(&mut *box_, (box_, Box::new(stor)))
    }

    #[inline]
    fn to_qemu_full(&self) -> *mut qapi_ffi::%(rs_name)s {
        unsafe {
            %(memb_full)s
            let ptr = ffi::g_malloc0(
                std::mem::size_of::<%(rs_name)s>()) as *mut _;
            *ptr = qapi_ffi::%(rs_name)s { %(ffi_memb)s };
            ptr
        }
    }
}
''',
                 cfg=ifcond.rsgen(),
                 rs_name=rs_name(name),
                 lt=lifetime,
                 ffi_memb=', '.join(ffi_memb),
                 memb_none=memb_none,
                 memb_full=memb_full)


def gen_rs_alternate(name: str,
                     ifcond: QAPISchemaIfCond,
                     variants: Optional[QAPISchemaVariants]) -> str:
    if name in objects_seen:
        return ''

    ret = ''
    objects_seen.add(name)

    ret += mcgen('''
%(cfg)s
#[derive(Clone, Debug)]
pub enum %(rs_name)s {
''',
                 cfg=ifcond.rsgen(),
                 rs_name=rs_name(name))

    for var in variants.variants:
        if var.type.name == 'q_empty':
            continue
        ret += mcgen('''
        %(cfg)s
        %(mem_name)s(%(rs_type)s),
''',
                     cfg=var.ifcond.rsgen(),
                     rs_type=rs_type(var.type.c_unboxed_type(), ''),
                     mem_name=to_camel_case(rs_name(var.name)))

    membs = ''
    lifetime = ''
    for var in variants.variants:
        var_name = to_camel_case(rs_name(var.name))
        type_name = var.type.name
        if type_name == 'q_empty':
            continue
        if not var.type.c_type().endswith(POINTER_SUFFIX):
            membs += mcgen('''
    %(cfg)s
    %(var_name)s,
''',
                           cfg=var.ifcond.rsgen(),
                           var_name=var_name)
        else:
            lifetime = "<'a>"
            c_type = var.type.c_type()
            ptr_ty = rs_ffitype(c_type)
            membs += mcgen('''
    %(cfg)s
    %(var_name)s(<%(rs_type)s as ToQemuPtr<'a, %(ptr_ty)s>>::Storage),
''',
                           cfg=var.ifcond.rsgen(),
                           var_name=var_name,
                           rs_type=rs_type(c_type, ''),
                           ptr_ty=ptr_ty)
    ret += mcgen('''
}

%(cfg)s
pub enum %(rs_name)sStorage%(lt)s {
    %(membs)s
}
''',
                 cfg=ifcond.rsgen(),
                 rs_name=rs_name(name),
                 lt=lifetime,
                 membs=membs)

    ret += gen_rs_alternate_from_qemu(name, ifcond, variants)
    ret += gen_rs_alternate_to_qemu(name, ifcond, variants, lifetime)

    return ret


def gen_rs_enum(name: str, ifcond: QAPISchemaIfCond) -> str:
    return mcgen('''

%(cfg)s
pub type %(rs_name)s = qapi_ffi::%(ffi_name)s;

%(cfg)s
impl_to_qemu_scalar_boxed!(%(rs_name)s);

%(cfg)s
impl_from_qemu_none_scalar!(%(rs_name)s);
''',
                 cfg=ifcond.rsgen(),
                 rs_name=rs_name(name),
                 ffi_name=rs_name(name))


class QAPISchemaGenRsTypeVisitor(QAPISchemaRsVisitor):

    def __init__(self, prefix: str) -> None:
        super().__init__(prefix, 'qapi-types')

    def visit_begin(self, schema: QAPISchema) -> None:
        # don't visit the empty type
        objects_seen.add(schema.the_empty_object_type.name)
        self._gen.preamble_add(
            mcgen('''
// generated by qapi-gen, DO NOT EDIT

use common::{QNull, QObject};
use crate::qapi_ffi;

'''))

    def visit_array_type(self,
                         name: str,
                         info: Optional[QAPISourceInfo],
                         ifcond: QAPISchemaIfCond,
                         element_type: QAPISchemaType) -> None:
        typ = rs_type(name, qapi_ns='')
        scalar = False
        if name[:-4] in {'number',
                         'int',
                         'int8',
                         'int16',
                         'int32',
                         'int64',
                         'uint8',
                         'uint16',
                         'uint32',
                         'uint64',
                         'size',
                         'bool'}:
            scalar = True
        if isinstance(element_type, QAPISchemaEnumType):
            scalar = True

        self._gen.add(mcgen('''
%(cfg)s
mod %(mod)s_module {
    use super::*;

    vec_type!(%(rs)s, %(ffi)s, qapi_free_%(name)s, %(scalar)i);
}

%(cfg)s
pub use %(mod)s_module::*;
''',
                            cfg=ifcond.rsgen(),
                            name=rs_name(name, protect=False),
                            mod=rs_name(name).lower(),
                            ffi=rs_name(name),
                            rs=typ,
                            scalar=scalar))

    def visit_object_type(self,
                          name: str,
                          info: Optional[QAPISourceInfo],
                          ifcond: QAPISchemaIfCond,
                          features: List[QAPISchemaFeature],
                          base: Optional[QAPISchemaObjectType],
                          members: List[QAPISchemaObjectTypeMember],
                          variants: Optional[QAPISchemaVariants]) -> None:
        if name.startswith('q_'):
            return
        self._gen.add(gen_rs_object(name, ifcond, base, members, variants))

    def visit_enum_type(self,
                        name: str,
                        info: Optional[QAPISourceInfo],
                        ifcond: QAPISchemaIfCond,
                        features: List[QAPISchemaFeature],
                        members: List[QAPISchemaEnumMember],
                        prefix: Optional[str]) -> None:
        self._gen.add(gen_rs_enum(name, ifcond))

    def visit_alternate_type(self,
                             name: str,
                             info: QAPISourceInfo,
                             ifcond: QAPISchemaIfCond,
                             features: List[QAPISchemaFeature],
                             variants: QAPISchemaVariants) -> None:
        self._gen.add(gen_rs_alternate(name, ifcond, variants))


def gen_rs_types(schema: QAPISchema, output_dir: str, prefix: str) -> None:
    vis = QAPISchemaGenRsTypeVisitor(prefix)
    schema.visit(vis)
    vis.write(output_dir)
