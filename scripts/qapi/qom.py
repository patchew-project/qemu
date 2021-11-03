"""
QAPI QOM boilerplate generator

Copyright (c) 2021 Red Hat Inc.

Authors:
 Kevin Wolf <kwolf@redhat.com>

This work is licensed under the terms of the GNU GPL, version 2.
See the COPYING file in the top-level directory.
"""

from .common import c_name, mcgen
from .gen import (
    build_params,
    QAPISchemaModularCVisitor,
)
from .schema import (
    QAPISchema,
    QAPISchemaClass,
)


def gen_config_decl(c: QAPISchemaClass) -> str:
    params = build_params(c.config_type, c.config_boxed,  'Error **errp')
    return mcgen('''
bool qom_%(name)s_marshal_config(Object *obj, Visitor *v, Error **errp);
bool qom_%(name)s_config(Object *obj, %(params)s);
''', name=c.c_name(), params=params)


def gen_config_call(c: QAPISchemaClass) -> str:
    if c.config_boxed:
        argstr = '&config, '
    else:
        assert not c.config_type.variants
        argstr = ''
        for m in c.config_type.members:
            if m.optional:
                argstr += 'config.has_%s, ' % c_name(m.name)
            argstr += 'config.%s, ' % c_name(m.name)

    return f'qom_{c.c_name()}_config(obj, {argstr}errp)'

def gen_config(c: QAPISchemaClass) -> str:
    return mcgen('''
bool qom_%(qom_type)s_marshal_config(Object *obj, Visitor *v, Error **errp)
{
    %(config_name)s config = {0};

    if (!visit_type_%(config_name)s_members(v, &config, errp)) {
        return false;
    }

    return %(call)s;
}

''', qom_type=c.c_name(), config_name=c.config_type.c_name(),
     call=gen_config_call(c))


class QAPISchemaGenQOMVisitor(QAPISchemaModularCVisitor):

    def __init__(self, prefix: str):
        super().__init__(
            prefix, 'qapi-qom', ' * Schema-defined QOM types',
            None, __doc__)

    def _begin_user_module(self, name: str) -> None:
        qom = self._module_basename('qapi-qom', name)
        types = self._module_basename('qapi-types', name)
        visit = self._module_basename('qapi-visit', name)
        self._genc.add(mcgen('''
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "%(qom)s.h"
#include "%(types)s.h"
#include "%(visit)s.h"

''', types=types, visit=visit, qom=qom))

    def visit_class(self, c: QAPISchemaClass) -> None:
        if c.config_type:
            self._genh.add(gen_config_decl(c))
            self._genc.add(gen_config(c))


def gen_qom(schema: QAPISchema, output_dir: str, prefix: str) -> None:
    vis = QAPISchemaGenQOMVisitor(prefix)
    schema.visit(vis)
    vis.write(output_dir)
