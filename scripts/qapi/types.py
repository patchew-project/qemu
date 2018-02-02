"""
QAPI types generator

Copyright IBM, Corp. 2011
Copyright (c) 2013-2018 Red Hat Inc.

Authors:
 Anthony Liguori <aliguori@us.ibm.com>
 Michael Roth <mdroth@linux.vnet.ibm.com>
 Markus Armbruster <armbru@redhat.com>

This work is licensed under the terms of the GNU GPL, version 2.
# See the COPYING file in the top-level directory.
"""

from qapi.common import *


# variants must be emitted before their container; track what has already
# been output
objects_seen = set()


def gen_fwd_object_or_array(name):
    return mcgen('''

typedef struct %(c_name)s %(c_name)s;
''',
                 c_name=c_name(name))


def gen_array(name, element_type):
    return mcgen('''

struct %(c_name)s {
    %(c_name)s *next;
    %(c_type)s value;
};
''',
                 c_name=c_name(name), c_type=element_type.c_type())


def gen_struct_members(members):
    ret = ''
    for memb in members:
        if memb.optional:
            ret += mcgen('''
    bool has_%(c_name)s;
''',
                         c_name=c_name(memb.name))
        ret += mcgen('''
    %(c_type)s %(c_name)s;
''',
                     c_type=memb.type.c_type(), c_name=c_name(memb.name))
    return ret


def gen_object(name, base, members, variants):
    if name in objects_seen:
        return ''
    objects_seen.add(name)

    ret = ''
    if variants:
        for v in variants.variants:
            if isinstance(v.type, QAPISchemaObjectType):
                ret += gen_object(v.type.name, v.type.base,
                                  v.type.local_members, v.type.variants)

    ret += mcgen('''

struct %(c_name)s {
''',
                 c_name=c_name(name))

    if base:
        if not base.is_implicit():
            ret += mcgen('''
    /* Members inherited from %(c_name)s: */
''',
                         c_name=base.c_name())
        ret += gen_struct_members(base.members)
        if not base.is_implicit():
            ret += mcgen('''
    /* Own members: */
''')
    ret += gen_struct_members(members)

    if variants:
        ret += gen_variants(variants)

    # Make sure that all structs have at least one member; this avoids
    # potential issues with attempting to malloc space for zero-length
    # structs in C, and also incompatibility with C++ (where an empty
    # struct is size 1).
    if (not base or base.is_empty()) and not members and not variants:
        ret += mcgen('''
    char qapi_dummy_for_empty_struct;
''')

    ret += mcgen('''
};
''')

    return ret


def gen_upcast(name, base):
    # C makes const-correctness ugly.  We have to cast away const to let
    # this function work for both const and non-const obj.
    return mcgen('''

static inline %(base)s *qapi_%(c_name)s_base(const %(c_name)s *obj)
{
    return (%(base)s *)obj;
}
''',
                 c_name=c_name(name), base=base.c_name())


def gen_variants(variants):
    ret = mcgen('''
    union { /* union tag is @%(c_name)s */
''',
                c_name=c_name(variants.tag_member.name))

    for var in variants.variants:
        ret += mcgen('''
        %(c_type)s %(c_name)s;
''',
                     c_type=var.type.c_unboxed_type(),
                     c_name=c_name(var.name))

    ret += mcgen('''
    } u;
''')

    return ret


def gen_type_cleanup_decl(name):
    ret = mcgen('''

void qapi_free_%(c_name)s(%(c_name)s *obj);
''',
                c_name=c_name(name))
    return ret


def gen_type_cleanup(name):
    ret = mcgen('''

void qapi_free_%(c_name)s(%(c_name)s *obj)
{
    Visitor *v;

    if (!obj) {
        return;
    }

    v = qapi_dealloc_visitor_new();
    visit_type_%(c_name)s(v, NULL, &obj, NULL);
    visit_free(v);
}
''',
                c_name=c_name(name))
    return ret


class QAPISchemaGenTypeVisitor(QAPISchemaVisitor):
    def __init__(self, opt_builtins, prefix):
        self._opt_builtins = opt_builtins
        self._prefix = prefix
        self._module = {}
        self._main_module = None
        self._add_module(None, ' * Built-in QAPI types')
        self._genc.preamble(mcgen('''
#include "qemu/osdep.h"
#include "qapi/dealloc-visitor.h"
#include "qapi-builtin-types.h"
#include "qapi-builtin-visit.h"
'''))
        self._genh.preamble(mcgen('''
#include "qapi/util.h"
'''))

    def _module_basename(self, name):
        if name is None:
            return 'qapi-builtin-types'
        basename = os.path.join(os.path.dirname(name),
                                self._prefix + 'qapi-types')
        if name == self._main_module:
            return basename
        return basename + '-' + os.path.splitext(os.path.basename(name))[0]

    def _add_module(self, name, blurb):
        genc = QAPIGenC(blurb, __doc__)
        genh = QAPIGenH(blurb, __doc__)
        self._module[name] = (genc, genh)
        self._set_module(name)

    def _set_module(self, name):
        self._genc, self._genh = self._module[name]

    def write(self, output_dir):
        for name in self._module:
            if name is None and not self._opt_builtins:
                continue
            basename = self._module_basename(name)
            (genc, genh) = self._module[name]
            genc.write(output_dir, basename + '.c')
            genh.write(output_dir, basename + '.h')

    def visit_begin(self, schema):
        # gen_object() is recursive, ensure it doesn't visit the empty type
        objects_seen.add(schema.the_empty_object_type.name)

    def visit_module(self, name):
        if self._main_module is None:
            self._main_module = name
        if name in self._module:
            self._set_module(name)
            return
        self._add_module(name, ' * Schema-defined QAPI types')
        self._genc.preamble(mcgen('''
#include "qemu/osdep.h"
#include "qapi/dealloc-visitor.h"
#include "%(prefix)sqapi-types.h"
#include "%(prefix)sqapi-visit.h"
''',
                                  prefix=self._prefix))
        self._genh.preamble(mcgen('''
#include "qapi-builtin-types.h"
'''))

    def visit_include(self, name, info):
        self._genh.preamble(mcgen('''
#include "%(basename)s.h"
''',
                                  basename=self._module_basename(name)))

    def _gen_type_cleanup(self, name):
        self._genh.body(gen_type_cleanup_decl(name))
        self._genc.body(gen_type_cleanup(name))

    def visit_enum_type(self, name, info, values, prefix):
        self._genh.preamble(gen_enum(name, values, prefix))
        self._genc.body(gen_enum_lookup(name, values, prefix))

    def visit_array_type(self, name, info, element_type):
        self._genh.preamble(gen_fwd_object_or_array(name))
        self._genh.body(gen_array(name, element_type))
        self._gen_type_cleanup(name)

    def visit_object_type(self, name, info, base, members, variants):
        # Nothing to do for the special empty builtin
        if name == 'q_empty':
            return
        self._genh.preamble(gen_fwd_object_or_array(name))
        self._genh.body(gen_object(name, base, members, variants))
        if base and not base.is_implicit():
            self._genh.body(gen_upcast(name, base))
        # TODO Worth changing the visitor signature, so we could
        # directly use rather than repeat type.is_implicit()?
        if not name.startswith('q_'):
            # implicit types won't be directly allocated/freed
            self._gen_type_cleanup(name)

    def visit_alternate_type(self, name, info, variants):
        self._genh.preamble(gen_fwd_object_or_array(name))
        self._genh.body(gen_object(name, None,
                                   [variants.tag_member], variants))
        self._gen_type_cleanup(name)


def gen_types(schema, output_dir, prefix, opt_builtins):
    vis = QAPISchemaGenTypeVisitor(opt_builtins, prefix)
    schema.visit(vis)
    vis.write(output_dir)
