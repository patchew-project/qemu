"""
QAPI Rust types generator
"""

from qapi.common import *
from qapi.rs import QAPISchemaRsVisitor, rs_systype, rs_name, from_qemu, rs_type, from_list


objects_seen = set()


def gen_rs_object(name, ifcond, base, members, variants):
    if name in objects_seen:
        return ''

    ret = ''
    objects_seen.add(name)
    has_options = False
    for memb in members:
        if memb.optional:
            has_options = True

    if variants:
        ret += 'variants TODO'

    derive = 'Serialize, Deserialize, Type'
    serde = 'serde'
    if has_options:
        derive = 'SerializeDict, DeserializeDict, TypeDict'
        serde = 'zvariant'

    ret += mcgen('''

#[derive(Clone, Debug)]
#[cfg_attr(feature = "dbus", derive(%(derive)s))]
pub struct %(rs_name)s {
''',
                 rs_name=rs_name(name), derive=derive)

    if base:
        ret += 'Base TODO'

    memb_names = []
    for memb in members:
        memb_names.append(rs_name(memb.name))
        rsname = rs_name(memb.name)
        if rsname != memb.name:
            ret += mcgen('''
   #[cfg_attr(feature = "dbus", %(serde)s(rename = "%(name)s"))]
''', serde=serde, name=memb.name)

        ret += mcgen('''
    pub %(rs_name)s: %(rs_type)s,
''',
                     rs_type=rs_type(memb.type.c_type(), '', optional=memb.optional), rs_name=rsname)

    ret += mcgen('''
}

impl FromQemuPtrFull<*mut qapi_sys::%(rs_name)s> for %(rs_name)s {
    unsafe fn from_qemu_full(sys: *mut qapi_sys::%(rs_name)s) -> Self {
        let ret = from_qemu_none(sys as *const _);
        qapi_sys::qapi_free_%(rs_name)s(sys);
        ret
    }
}

impl FromQemuPtrNone<*const qapi_sys::%(rs_name)s> for %(rs_name)s {
    unsafe fn from_qemu_none(sys: *const qapi_sys::%(rs_name)s) -> Self {
        let sys = & *sys;
''', rs_name=rs_name(name))

    for memb in members:
        memb_name = rs_name(memb.name)
        val = from_qemu('sys.' + memb_name, memb.type.c_type())
        if memb.optional:
            val = mcgen('''{
            if sys.has_%(memb_name)s {
                Some(%(val)s)
            } else {
                None
            }
}''', memb_name=memb_name, val=val)

        ret += mcgen('''
        let %(memb_name)s = %(val)s;
''', memb_name=memb_name, val=val)

    ret += mcgen('''
            Self { %(memb_names)s }
        }
}
''', rs_name=rs_name(name), memb_names=', '.join(memb_names))

    storage = []
    stash = []
    sys_memb = []
    memb_none = ''
    memb_full = ''
    for memb in members:
        memb_name = rs_name(memb.name)
        c_type = memb.type.c_type()
        is_pointer = c_type.endswith(pointer_suffix)
        if is_pointer:
            t = rs_type(memb.type.c_type(), optional=memb.optional, ns='')
            p = rs_systype(memb.type.c_type())
            s = "translate::Stash<'a, %s, %s>" % (p, t)
            storage.append(s)
        if memb.optional:
            sys_memb.append('has_%s' % memb_name)
            has_memb = mcgen('''
    let has_%(memb_name)s = self.%(memb_name)s.is_some();
''', memb_name=memb_name)
            memb_none += has_memb
            memb_full += has_memb

        to_qemu = ''
        if is_pointer:
            memb_none += mcgen('''
    let %(memb_name)s_stash_ = self.%(memb_name)s.to_qemu_none();
    let %(memb_name)s = %(memb_name)s_stash_.0;
''', memb_name=memb_name)
            stash.append('%s_stash_' % memb_name)
            memb_full += mcgen('''
    let %(memb_name)s = self.%(memb_name)s.to_qemu_full();
''', memb_name=memb_name)
        else:
            unwrap = ''
            if memb.optional:
                unwrap = '.unwrap_or_default()'
            memb = mcgen('''
    let %(memb_name)s = self.%(memb_name)s%(unwrap)s;
''', memb_name=memb_name, unwrap=unwrap)
            memb_none += memb
            memb_full += memb

        sys_memb.append(memb_name)

    ret += mcgen('''

impl translate::QemuPtrDefault for %(rs_name)s {
    type QemuType = *mut qapi_sys::%(rs_name)s;
}

impl<'a> translate::ToQemuPtr<'a, *mut qapi_sys::%(rs_name)s> for %(rs_name)s {
    type Storage = (Box<qapi_sys::%(rs_name)s>, %(storage)s);

    #[inline]
    fn to_qemu_none(&'a self) -> translate::Stash<'a, *mut qapi_sys::%(rs_name)s, %(rs_name)s> {
        %(memb_none)s
        let mut box_ = Box::new(qapi_sys::%(rs_name)s { %(sys_memb)s });

        translate::Stash(&mut *box_, (box_, %(stash)s))
    }

    #[inline]
    fn to_qemu_full(&self) -> *mut qapi_sys::%(rs_name)s {
        unsafe {
            %(memb_full)s
            let ptr = qemu_sys::g_malloc0(std::mem::size_of::<*const %(rs_name)s>()) as *mut _;
            *ptr = qapi_sys::%(rs_name)s { %(sys_memb)s };
            ptr
        }
    }
}
''', rs_name=rs_name(name), storage=', '.join(storage),
                 sys_memb=', '.join(sys_memb), memb_none=memb_none, memb_full=memb_full, stash=', '.join(stash))

    return ret


def gen_rs_variant(name, ifcond, variants):
    if name in objects_seen:
        return ''

    ret = ''
    objects_seen.add(name)

    ret += mcgen('''

// Implement manual Value conversion (other option via some zbus macros?)
#[cfg(feature = "dbus")]
impl zvariant::Type for %(rs_name)s {
    fn signature() -> zvariant::Signature<'static> {
        zvariant::Value::signature()
    }
}

#[derive(Clone,Debug)]
#[cfg_attr(feature = "dbus", derive(Deserialize, Serialize), serde(into = "zvariant::OwnedValue", try_from = "zvariant::OwnedValue"))]
pub enum %(rs_name)s {
''',
                 rs_name=rs_name(name))

    for var in variants.variants:
        if var.type.name == 'q_empty':
            continue
        ret += mcgen('''
        %(mem_name)s(%(rs_type)s),
''',
                     rs_type=rs_type(var.type.c_unboxed_type(), ''),
                     mem_name=to_camel_case(rs_name(var.name)))
    ret += mcgen('''
}
''')
    return ret


class QAPISchemaGenRsTypeVisitor(QAPISchemaRsVisitor):

    def __init__(self, prefix):
        super().__init__(prefix, 'qapi-types')

    def visit_begin(self, schema):
        # gen_object() is recursive, ensure it doesn't visit the empty type
        objects_seen.add(schema.the_empty_object_type.name)
        self._gen.preamble_add(
            mcgen('''
// generated by qapi-gen, DO NOT EDIT
'''))

    def visit_end(self):
        for c_type in from_list:
            sys = rs_systype(c_type, sys_ns='')[5:]
            rs = rs_type(c_type, ns='')

            self._gen.add(mcgen('''

impl FromQemuPtrFull<*mut qapi_sys::%(sys)s> for %(rs)s {
    #[inline]
    unsafe fn from_qemu_full(sys: *mut qapi_sys::%(sys)s) -> Self {
        let ret = from_qemu_none(sys as *const _);
        qapi_sys::qapi_free_%(sys)s(sys);
        ret
    }
}

impl FromQemuPtrNone<*const qapi_sys::%(sys)s> for %(rs)s {
    #[inline]
    unsafe fn from_qemu_none(sys: *const qapi_sys::%(sys)s) -> Self {
         let mut ret = vec![];
         let mut it = sys;
         while !it.is_null() {
             let e = &*it;
             ret.push(translate::from_qemu_none(e.value as *const _));
             it = e.next;
         }
         ret
    }
}
''', sys=sys, rs=rs))

    def visit_command(self, name, info, ifcond, features,
                      arg_type, ret_type, gen, success_response, boxed,
                      allow_oob, allow_preconfig):
        if not gen:
            return
        # hack: eventually register a from_list
        if ret_type:
            from_qemu('', ret_type.c_type())

    def visit_object_type(self, name, info, ifcond, features,
                          base, members, variants):
        if name.startswith('q_'):
            return
        self._gen.add(gen_rs_object(name, ifcond, base, members, variants))

    def visit_enum_type(self, name, info, ifcond, features, members, prefix):
        self._gen.add(mcgen('''

pub type %(rs_name)s = qapi_sys::%(rs_name)s;
''', rs_name=rs_name(name)))

    def visit_alternate_type(self, name, info, ifcond, features, variants):
        self._gen.add(gen_rs_variant(name, ifcond, variants))


def gen_rs_types(schema, output_dir, prefix, opt_builtins):
    vis = QAPISchemaGenRsTypeVisitor(prefix)
    schema.visit(vis)
    vis.write(output_dir)
