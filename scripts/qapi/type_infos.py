"""
QAPI type info generator

SPDX-License-Identifier: GPL-2.0-or-later
"""

from typing import (
    Dict,
    List,
    Optional,
    Set,
)

from .common import c_name, mcgen
from .gen import QAPISchemaModularCVisitor, ifcontext
from .schema import (
    QAPISchema,
    QAPISchemaAlternatives,
    QAPISchemaBranches,
    QAPISchemaEnumMember,
    QAPISchemaFeature,
    QAPISchemaIfCond,
    QAPISchemaObjectType,
    QAPISchemaObjectTypeMember,
    QAPISchemaType,
    QAPISchemaVisitor,
)
from .source import QAPISourceInfo


class _ArrayTypeCollector(QAPISchemaVisitor):
    def __init__(self) -> None:
        self.list_types: Set[str] = set()

    def visit_array_type(self,
                         name: str,
                         info: Optional[QAPISourceInfo],
                         ifcond: QAPISchemaIfCond,
                         element_type: QAPISchemaType) -> None:
        self.list_types.add(name)


class QAPISchemaGenTypeInfoVisitor(QAPISchemaModularCVisitor):

    def __init__(self, prefix: str, name_map: Dict[str, str],
                 list_types: Set[str]):
        super().__init__(
            prefix, 'qapi-type-infos',
            ' * Schema-defined QAPI type info',
            ' * Built-in QAPI type info', __doc__)
        self._name_map = name_map
        self._list_types = list_types

    def _begin_builtin_module(self) -> None:
        self._genc.preamble_add(mcgen('''
#include "qemu/osdep.h"
#include "qapi/qapi-builtin-types.h"
#include "qapi/qapi-builtin-type-infos.h"
'''))
        self._genh.preamble_add(mcgen('''
#include "qapi/qapi-type-info.h"
'''))

    def _begin_user_module(self, name: str) -> None:
        type_infos = self._module_basename('qapi-type-infos', name)
        types = self._module_basename('qapi-types', name)
        self._genc.preamble_add(mcgen('''
#include "qemu/osdep.h"
#include "%(types)s.h"
#include "%(type_infos)s.h"
''',
                                      types=types,
                                      type_infos=type_infos))
        self._genh.preamble_add(mcgen('''
#include "qapi/qapi-builtin-type-infos.h"
'''))

    def _gen_type_info(self, name: str,
                       ifcond: Optional[QAPISchemaIfCond] = None,
                       with_lookup: bool = False,
                       with_list: bool = False,
                       schema_name: Optional[str] = None) -> None:
        c_id = c_name(name + '_type_info')
        lookup = ''
        if with_lookup:
            lookup = mcgen('''
    .lookup = &%(c_name)s_lookup,
''',
                           c_name=c_name(name))
        list_ref = ''
        if with_list:
            list_ref = mcgen('''
    .list = &%(list_id)s,
''',
                             list_id=c_name(name + 'List_type_info'))
        if schema_name is None:
            masked = self._name_map.get(name)
            schema_name = '"%s"' % masked if masked is not None else 'NULL'
        else:
            schema_name = '"%s"' % schema_name
        with ifcontext(ifcond or QAPISchemaIfCond(),
                       self._genh, self._genc):
            self._genh.add(mcgen('''

extern const QAPITypeInfo %(c_id)s;
''',
                                 c_id=c_id))
            self._genc.add(mcgen('''

const QAPITypeInfo %(c_id)s = {
    .name = "%(name)s",
    .schema_name = %(schema_name)s,
%(lookup)s%(list_ref)s};
''',
                                 c_id=c_id, name=name,
                                 schema_name=schema_name,
                                 lookup=lookup,
                                 list_ref=list_ref))

    def _has_list(self, name: str) -> bool:
        return name + 'List' in self._list_types

    def visit_builtin_type(self,
                           name: str,
                           info: Optional[QAPISourceInfo],
                           json_type: str) -> None:
        self._gen_type_info(name, with_list=self._has_list(name))

    def visit_enum_type(self,
                        name: str,
                        info: Optional[QAPISourceInfo],
                        ifcond: QAPISchemaIfCond,
                        features: List[QAPISchemaFeature],
                        members: List[QAPISchemaEnumMember],
                        prefix: Optional[str]) -> None:
        self._gen_type_info(name, ifcond, with_lookup=True,
                            with_list=self._has_list(name))

    def visit_object_type(self,
                          name: str,
                          info: Optional[QAPISourceInfo],
                          ifcond: QAPISchemaIfCond,
                          features: List[QAPISchemaFeature],
                          base: Optional[QAPISchemaObjectType],
                          members: List[QAPISchemaObjectTypeMember],
                          branches: Optional[QAPISchemaBranches]) -> None:
        if name.startswith('q_'):
            return
        self._gen_type_info(name, ifcond,
                            with_list=self._has_list(name))

    def visit_array_type(self,
                         name: str,
                         info: Optional[QAPISourceInfo],
                         ifcond: QAPISchemaIfCond,
                         element_type: QAPISchemaType) -> None:
        elem_schema = self._name_map.get(element_type.name,
                                         element_type.name)
        self._gen_type_info(name, ifcond,
                            schema_name='[' + elem_schema + ']')

    def visit_alternate_type(self,
                             name: str,
                             info: Optional[QAPISourceInfo],
                             ifcond: QAPISchemaIfCond,
                             features: List[QAPISchemaFeature],
                             alternatives: QAPISchemaAlternatives) -> None:
        self._gen_type_info(name, ifcond,
                            with_list=self._has_list(name))


def gen_type_infos(schema: QAPISchema,
                   output_dir: str,
                   prefix: str,
                   opt_builtins: bool,
                   name_map: Dict[str, str]) -> None:
    collector = _ArrayTypeCollector()
    schema.visit(collector)
    vis = QAPISchemaGenTypeInfoVisitor(prefix, name_map,
                                       collector.list_types)
    schema.visit(vis)
    vis.write(output_dir, opt_builtins)
