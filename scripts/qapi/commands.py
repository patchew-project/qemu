"""
QAPI command marshaller generator

Copyright IBM, Corp. 2011
Copyright (C) 2014-2018 Red Hat, Inc.

Authors:
 Anthony Liguori <aliguori@us.ibm.com>
 Michael Roth <mdroth@linux.vnet.ibm.com>
 Markus Armbruster <armbru@redhat.com>

This work is licensed under the terms of the GNU GPL, version 2.
See the COPYING file in the top-level directory.
"""

from qapi.common import *


def gen_command_decl(name, arg_type, boxed, ret_type, success_response, asyn):
    if asyn:
        extra = "QmpReturn *qret"
    else:
        extra = 'Error **errp'

    if asyn:
        ret = mcgen('''
void qmp_%(name)s(%(params)s);
''',
                     name=c_name(name),
                     params=build_params(arg_type, boxed, extra))
        if success_response:
            ret += mcgen('''
void qmp_%(name)s_return(QmpReturn *qret%(c_type)s);
''',
                        c_type=(", " + ret_type.c_type() if ret_type else ""),
                        name=c_name(name))

        return ret
    else:
        return mcgen('''
%(c_type)s qmp_%(c_name)s(%(params)s);
''',
                     c_type=(ret_type and ret_type.c_type()) or 'void',
                     c_name=c_name(name),
                     params=build_params(arg_type, boxed, extra))


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


def build_marshal_proto(name, asyn):
    if asyn:
        tmpl = 'void qmp_marshal_%s(QDict *args, QmpReturn *qret)'
    else:
        tmpl = 'void qmp_marshal_%s(QDict *args, QObject **ret, Error **errp)'
    return tmpl % c_name(name)


def gen_marshal_decl(name, asyn):
    return mcgen('''
%(proto)s;
''',
                 proto=build_marshal_proto(name, asyn))


def gen_marshal(name, arg_type, boxed, ret_type, asyn):
    have_args = arg_type and not arg_type.is_empty()

    ret = mcgen('''

%(proto)s
{
    Error *err = NULL;
''',
                proto=build_marshal_proto(name, asyn))

    if ret_type and not asyn:
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
    v = qobject_input_visitor_new(QOBJECT(args));
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

    if asyn:
        ret += gen_async_call(name, arg_type, boxed)
    else:
        ret += gen_call(name, arg_type, boxed, ret_type)

    ret += mcgen('''

out:
''')

    if asyn:
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


def gen_register_command(name, success_response, allow_oob, allow_preconfig,
                         asyn):
    options = []

    if not success_response:
        options += ['QCO_NO_SUCCESS_RESP']
    if allow_oob:
        options += ['QCO_ALLOW_OOB']
    if allow_preconfig:
        options += ['QCO_ALLOW_PRECONFIG']
    if asyn:
        options += ['QCO_ASYNC']

    if not options:
        options = ['QCO_NO_OPTIONS']

    options = " | ".join(options)

    if asyn:
        regfn = 'qmp_register_async_command'
    else:
        regfn = 'qmp_register_command'

    ret = mcgen('''
    %(regfn)s(cmds, "%(name)s",
                         qmp_marshal_%(c_name)s, %(opts)s);
''',
                regfn=regfn, name=name, c_name=c_name(name),
                opts=options)
    return ret


def gen_registry(registry, prefix):
    ret = mcgen('''

void %(c_prefix)sqmp_init_marshal(QmpCommandList *cmds)
{
    QTAILQ_INIT(cmds);

''',
                c_prefix=c_name(prefix, protect=False))
    ret += registry
    ret += mcgen('''
}
''')
    return ret


class QAPISchemaGenCommandVisitor(QAPISchemaModularCVisitor):

    def __init__(self, prefix):
        QAPISchemaModularCVisitor.__init__(
            self, prefix, 'qapi-commands',
            ' * Schema-defined QAPI/QMP commands', __doc__)
        self._regy = QAPIGenCCode(None)
        self._visited_ret_types = {}

    def _begin_user_module(self, name):
        self._visited_ret_types[self._genc] = set()
        commands = self._module_basename('qapi-commands', name)
        types = self._module_basename('qapi-types', name)
        visit = self._module_basename('qapi-visit', name)
        self._genc.add(mcgen('''
#include "qemu/osdep.h"
#include "qapi/visitor.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qobject-output-visitor.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/dealloc-visitor.h"
#include "qapi/error.h"
#include "%(visit)s.h"
#include "%(commands)s.h"

''',
                             commands=commands, visit=visit))
        self._genh.add(mcgen('''
#include "%(types)s.h"
#include "qapi/qmp/dispatch.h"

''',
                             types=types))

    def visit_end(self):
        (genc, genh) = self._module[self._main_module]
        genh.add(mcgen('''
void %(c_prefix)sqmp_init_marshal(QmpCommandList *cmds);
''',
                       c_prefix=c_name(self._prefix, protect=False)))
        genc.add(gen_registry(self._regy.get_content(), self._prefix))

    def visit_command(self, name, info, ifcond, arg_type, ret_type, gen,
                      success_response, boxed, allow_oob, allow_preconfig,
                      asyn):
        if not gen:
            return
        # FIXME: If T is a user-defined type, the user is responsible
        # for making this work, i.e. to make T's condition the
        # conjunction of the T-returning commands' conditions.  If T
        # is a built-in type, this isn't possible: the
        # qmp_marshal_output_T() will be generated unconditionally.
        if ret_type and ret_type not in self._visited_ret_types[self._genc]:
            self._visited_ret_types[self._genc].add(ret_type)
            with ifcontext(ret_type.ifcond,
                           self._genh, self._genc, self._regy):
                self._genc.add(gen_marshal_output(ret_type))
        with ifcontext(ifcond, self._genh, self._genc, self._regy):
            self._genh.add(gen_command_decl(name, arg_type, boxed, ret_type,
                                            success_response, asyn))
            self._genh.add(gen_marshal_decl(name, asyn))
            self._genc.add(gen_marshal(name, arg_type, boxed, ret_type, asyn))
            if asyn and success_response:
                self._genc.add(gen_async_return(name, ret_type))
            self._regy.add(gen_register_command(name, success_response,
                                                allow_oob, allow_preconfig,
                                                asyn))


def gen_commands(schema, output_dir, prefix):
    vis = QAPISchemaGenCommandVisitor(prefix)
    schema.visit(vis)
    vis.write(output_dir)
