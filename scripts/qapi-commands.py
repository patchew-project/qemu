#
# QAPI command marshaller generator
#
# Copyright IBM, Corp. 2011
# Copyright (C) 2014-2016 Red Hat, Inc.
#
# Authors:
#  Anthony Liguori <aliguori@us.ibm.com>
#  Michael Roth    <mdroth@linux.vnet.ibm.com>
#  Markus Armbruster <armbru@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.
# See the COPYING file in the top-level directory.

from qapi import *
import re


def gen_command_decl(name, arg_type, boxed, ret_type, async):
    if async:
        extra = "QmpReturn *qret"
    else:
        extra = 'Error **errp'

    if async:
        return mcgen('''
void qmp_%(name)s(%(params)s);
void qmp_%(name)s_return(QmpReturn *qret%(c_type)s);
''',
                     c_type=(", " + ret_type.c_type() if ret_type else ""),
                     name=c_name(name),
                     params=gen_params(arg_type, boxed, extra))
    else:
        return mcgen('''
%(c_type)s qmp_%(c_name)s(%(params)s);
''',
                     c_type=(ret_type and ret_type.c_type()) or 'void',
                     c_name=c_name(name),
                     params=gen_params(arg_type, boxed, extra))


def gen_argstr(arg_type, boxed):
    argstr = ''
    if boxed:
        assert arg_type and not arg_type.is_empty()
        argstr = '&arg, '
    elif arg_type:
        assert not arg_type.variants
        for memb in arg_type.members:
            if memb.optional:
                argstr += 'arg.has_%s, ' % c_name(memb.name)
            argstr += 'arg.%s, ' % c_name(memb.name)

    return argstr


def gen_call(name, arg_type, boxed, ret_type):
    ret = ''

    argstr = gen_argstr(arg_type, boxed)
    lhs = ''
    if ret_type:
        lhs = 'retval = '

    ret = mcgen('''

    %(lhs)sqmp_%(c_name)s(%(args)s&err);
''',
                c_name=c_name(name), args=argstr, lhs=lhs)
    if ret_type:
        ret += mcgen('''
    if (err) {
        goto out;
    }

    qmp_marshal_output_%(c_name)s(retval, ret, &err);
''',
                     c_name=ret_type.c_name())
    return ret


def gen_async_call(name, arg_type, boxed):
    argstr = gen_argstr(arg_type, boxed)

    push_indent()
    ret = mcgen('''

qmp_%(c_name)s(%(args)sqret);
''',
                c_name=c_name(name), args=argstr)

    pop_indent()
    return ret


def gen_async_return(name, ret_type):
    if ret_type:
        return mcgen('''

void qmp_%(c_name)s_return(QmpReturn *qret, %(ret_type)s ret_in)
{
    Error *err = NULL;
    QObject *ret_out = NULL;

    qmp_marshal_output_%(ret_c_name)s(ret_in, &ret_out, &err);

    if (err) {
        qmp_return_error(qret, err);
    } else {
        qmp_return(qret, ret_out);
    }
}
''',
                     c_name=c_name(name),
                     ret_type=ret_type.c_type(), ret_c_name=ret_type.c_name())
    else:
        return mcgen('''

void qmp_%(c_name)s_return(QmpReturn *qret)
{
    qmp_return(qret, QOBJECT(qdict_new()));
}
''',
                     c_name=c_name(name))

def gen_marshal_output(ret_type):
    return mcgen('''

static void qmp_marshal_output_%(c_name)s(%(c_type)s ret_in, QObject **ret_out, Error **errp)
{
    Error *err = NULL;
    Visitor *v;

    v = qobject_output_visitor_new(ret_out);
    visit_type_%(c_name)s(v, "unused", &ret_in, &err);
    if (!err) {
        visit_complete(v, ret_out);
    }
    error_propagate(errp, err);
    visit_free(v);
    v = qapi_dealloc_visitor_new();
    visit_type_%(c_name)s(v, "unused", &ret_in, NULL);
    visit_free(v);
}
''',
                 c_type=ret_type.c_type(), c_name=ret_type.c_name())


def gen_marshal_proto(name, async):
    if async:
        tmpl = 'void qmp_marshal_%s(QDict *args, QmpReturn *qret)'
    else:
        tmpl = 'void qmp_marshal_%s(QDict *args, QObject **ret, Error **errp)'
    return tmpl % c_name(name)


def gen_marshal_decl(name, async):
    return mcgen('''
%(proto)s;
''',
                 proto=gen_marshal_proto(name, async))


def gen_marshal(name, arg_type, boxed, ret_type, async):
    have_args = arg_type and not arg_type.is_empty()

    ret = mcgen('''

%(proto)s
{
    Error *err = NULL;
''',
                proto=gen_marshal_proto(name, async))

    if ret_type and not async:
        ret += mcgen('''
    %(c_type)s retval;
''',
                     c_type=ret_type.c_type())

    if have_args:
        visit_members = ('visit_type_%s_members(v, &arg, &err);'
                         % arg_type.c_name())
        ret += mcgen('''
    Visitor *v;
    %(c_name)s arg = {0};

''',
                     c_name=arg_type.c_name())
    else:
        visit_members = ''
        ret += mcgen('''
    Visitor *v = NULL;

    if (args) {
''')
        push_indent()

    ret += mcgen('''
    v = qobject_input_visitor_new(QOBJECT(args), true);
    visit_start_struct(v, NULL, NULL, 0, &err);
    if (err) {
        goto out;
    }
    %(visit_members)s
    if (!err) {
        visit_check_struct(v, &err);
    }
    visit_end_struct(v, NULL);
    if (err) {
        goto out;
    }
''',
                 visit_members=visit_members)

    if not have_args:
        pop_indent()
        ret += mcgen('''
    }
''')

    if async:
        ret += gen_async_call(name, arg_type, boxed)
    else:
        ret += gen_call(name, arg_type, boxed, ret_type)

    ret += mcgen('''

out:
''')

    if async:
         ret += mcgen('''
    if (err) {
        qmp_return_error(qret, err);
    }
''')
    else:
        ret += mcgen('''
    error_propagate(errp, err);
''')

    ret += mcgen('''
    visit_free(v);
''')

    if have_args:
        visit_members = ('visit_type_%s_members(v, &arg, NULL);'
                         % arg_type.c_name())
    else:
        visit_members = ''
        ret += mcgen('''
    if (args) {
''')
        push_indent()

    ret += mcgen('''
    v = qapi_dealloc_visitor_new();
    visit_start_struct(v, NULL, NULL, 0, NULL);
    %(visit_members)s
    visit_end_struct(v, NULL);
    visit_free(v);
''',
                 visit_members=visit_members)

    if not have_args:
        pop_indent()
        ret += mcgen('''
    }
''')

    ret += mcgen('''
}
''')
    return ret


def gen_register_command(name, success_response, async):
    options = 'QCO_NO_OPTIONS'
    if not success_response:
        options = 'QCO_NO_SUCCESS_RESP'
    func = 'qmp_register_command'
    if async:
        func = 'qmp_register_async_command'
    ret = mcgen('''
    %(func)s("%(name)s", qmp_marshal_%(c_name)s, %(opts)s);
''',
                func=func, name=name, c_name=c_name(name),
                opts=options)
    return ret


def gen_registry(registry):
    ret = mcgen('''

static void qmp_init_marshal(void)
{
''')
    ret += registry
    ret += mcgen('''
}

qapi_init(qmp_init_marshal);
''')
    return ret


class QAPISchemaGenCommandVisitor(QAPISchemaVisitor):
    def __init__(self):
        self.decl = None
        self.defn = None
        self._regy = None
        self._visited_ret_types = None

    def visit_begin(self, schema):
        self.decl = ''
        self.defn = ''
        self._regy = ''
        self._visited_ret_types = set()

    def visit_end(self):
        self.defn += gen_registry(self._regy)
        self._regy = None
        self._visited_ret_types = None

    def visit_command(self, name, info, arg_type, ret_type,
                      gen, success_response, boxed, async):
        if not gen:
            return
        self.decl += gen_command_decl(name, arg_type, boxed,
                                      ret_type, async)
        if ret_type and ret_type not in self._visited_ret_types:
            self._visited_ret_types.add(ret_type)
            self.defn += gen_marshal_output(ret_type)
        if async:
            self.defn += gen_async_return(name, ret_type)
        self.decl += gen_marshal_decl(name, async)
        self.defn += gen_marshal(name, arg_type, boxed, ret_type, async)
        self._regy += gen_register_command(name, success_response, async)


(input_file, output_dir, do_c, do_h, prefix, opts) = parse_command_line()

c_comment = '''
/*
 * schema-defined QMP->QAPI command dispatch
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */
'''
h_comment = '''
/*
 * schema-defined QAPI function prototypes
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */
'''

(fdef, fdecl) = open_output(output_dir, do_c, do_h, prefix,
                            'qmp-marshal.c', 'qmp-commands.h',
                            c_comment, h_comment)

fdef.write(mcgen('''
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/module.h"
#include "qapi/qmp/types.h"
#include "qapi/qmp/dispatch.h"
#include "qapi/visitor.h"
#include "qapi/qobject-output-visitor.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/dealloc-visitor.h"
#include "%(prefix)sqapi-types.h"
#include "%(prefix)sqapi-visit.h"
#include "%(prefix)sqmp-commands.h"

''',
                 prefix=prefix))

fdecl.write(mcgen('''
#include "%(prefix)sqapi-types.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/dispatch.h"
#include "qapi/error.h"

''',
                  prefix=prefix))

schema = QAPISchema(input_file)
gen = QAPISchemaGenCommandVisitor()
schema.visit(gen)
fdef.write(gen.defn)
fdecl.write(gen.decl)

close_output(fdef, fdecl)
