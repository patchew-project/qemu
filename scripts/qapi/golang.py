"""
Golang QAPI generator
"""
# Copyright (c) 2023 Red Hat Inc.
#
# Authors:
#  Victor Toso <victortoso@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.
# See the COPYING file in the top-level directory.

# due QAPISchemaVisitor interface
# pylint: disable=too-many-arguments

# Just for type hint on self
from __future__ import annotations

import os
from typing import List, Optional

from .schema import (
    QAPISchema,
    QAPISchemaType,
    QAPISchemaVisitor,
    QAPISchemaEnumMember,
    QAPISchemaFeature,
    QAPISchemaIfCond,
    QAPISchemaObjectType,
    QAPISchemaObjectTypeMember,
    QAPISchemaVariants,
)
from .source import QAPISourceInfo

TEMPLATE_ENUM = '''
type {name} string
const (
{fields}
)
'''


def gen_golang(schema: QAPISchema,
               output_dir: str,
               prefix: str) -> None:
    vis = QAPISchemaGenGolangVisitor(prefix)
    schema.visit(vis)
    vis.write(output_dir)


def qapi_to_field_name_enum(name: str) -> str:
    return name.title().replace("-", "")


class QAPISchemaGenGolangVisitor(QAPISchemaVisitor):

    def __init__(self, _: str):
        super().__init__()
        types = ["enum"]
        self.target = {name: "" for name in types}
        self.schema = None
        self.golang_package_name = "qapi"

    def visit_begin(self, schema):
        self.schema = schema

        # Every Go file needs to reference its package name
        for target in self.target:
            self.target[target] = f"package {self.golang_package_name}\n"

    def visit_end(self):
        self.schema = None

    def visit_object_type(self: QAPISchemaGenGolangVisitor,
                          name: str,
                          info: Optional[QAPISourceInfo],
                          ifcond: QAPISchemaIfCond,
                          features: List[QAPISchemaFeature],
                          base: Optional[QAPISchemaObjectType],
                          members: List[QAPISchemaObjectTypeMember],
                          variants: Optional[QAPISchemaVariants]
                          ) -> None:
        pass

    def visit_alternate_type(self: QAPISchemaGenGolangVisitor,
                             name: str,
                             info: Optional[QAPISourceInfo],
                             ifcond: QAPISchemaIfCond,
                             features: List[QAPISchemaFeature],
                             variants: QAPISchemaVariants
                             ) -> None:
        pass

    def visit_enum_type(self: QAPISchemaGenGolangVisitor,
                        name: str,
                        info: Optional[QAPISourceInfo],
                        ifcond: QAPISchemaIfCond,
                        features: List[QAPISchemaFeature],
                        members: List[QAPISchemaEnumMember],
                        prefix: Optional[str]
                        ) -> None:

        value = qapi_to_field_name_enum(members[0].name)
        fields = ""
        for member in members:
            value = qapi_to_field_name_enum(member.name)
            fields += f'''\t{name}{value} {name} = "{member.name}"\n'''

        self.target["enum"] += TEMPLATE_ENUM.format(name=name, fields=fields[:-1])

    def visit_array_type(self, name, info, ifcond, element_type):
        pass

    def visit_command(self,
                      name: str,
                      info: Optional[QAPISourceInfo],
                      ifcond: QAPISchemaIfCond,
                      features: List[QAPISchemaFeature],
                      arg_type: Optional[QAPISchemaObjectType],
                      ret_type: Optional[QAPISchemaType],
                      gen: bool,
                      success_response: bool,
                      boxed: bool,
                      allow_oob: bool,
                      allow_preconfig: bool,
                      coroutine: bool) -> None:
        pass

    def visit_event(self, name, info, ifcond, features, arg_type, boxed):
        pass

    def write(self, output_dir: str) -> None:
        for module_name, content in self.target.items():
            go_module = module_name + "s.go"
            go_dir = "go"
            pathname = os.path.join(output_dir, go_dir, go_module)
            odir = os.path.dirname(pathname)
            os.makedirs(odir, exist_ok=True)

            with open(pathname, "w", encoding="ascii") as outfile:
                outfile.write(content)
