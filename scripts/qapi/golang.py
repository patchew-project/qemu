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

# Just for type hint on self
from __future__ import annotations

import os
from typing import List, Optional

from .schema import (
    QAPISchema,
    QAPISchemaEnumMember,
    QAPISchemaFeature,
    QAPISchemaIfCond,
    QAPISchemaObjectType,
    QAPISchemaObjectTypeMember,
    QAPISchemaType,
    QAPISchemaVariants,
    QAPISchemaVisitor,
)
from .source import QAPISourceInfo


TEMPLATE_ENUM = """
type {name} string

const (
{fields}
)
"""


def gen_golang(schema: QAPISchema, output_dir: str, prefix: str) -> None:
    vis = QAPISchemaGenGolangVisitor(prefix)
    schema.visit(vis)
    vis.write(output_dir)


def qapi_to_field_name_enum(name: str) -> str:
    return name.title().replace("-", "")


def generate_content_from_dict(data: dict[str, str]) -> str:
    content = ""

    for name in sorted(data):
        content += data[name]

    return content


class QAPISchemaGenGolangVisitor(QAPISchemaVisitor):
    # pylint: disable=too-many-arguments
    def __init__(self, _: str):
        super().__init__()
        types = ("enum",)
        self.target = dict.fromkeys(types, "")
        self.schema: QAPISchema
        self.golang_package_name = "qapi"
        self.enums: dict[str, str] = {}

    def visit_begin(self, schema: QAPISchema) -> None:
        self.schema = schema

        # Every Go file needs to reference its package name
        for target in self.target:
            self.target[target] = f"package {self.golang_package_name}\n"

    def visit_end(self) -> None:
        del self.schema
        self.target["enum"] += generate_content_from_dict(self.enums)

    def visit_object_type(
        self,
        name: str,
        info: Optional[QAPISourceInfo],
        ifcond: QAPISchemaIfCond,
        features: List[QAPISchemaFeature],
        base: Optional[QAPISchemaObjectType],
        members: List[QAPISchemaObjectTypeMember],
        variants: Optional[QAPISchemaVariants],
    ) -> None:
        pass

    def visit_alternate_type(
        self,
        name: str,
        info: Optional[QAPISourceInfo],
        ifcond: QAPISchemaIfCond,
        features: List[QAPISchemaFeature],
        variants: QAPISchemaVariants,
    ) -> None:
        pass

    def visit_enum_type(
        self,
        name: str,
        info: Optional[QAPISourceInfo],
        ifcond: QAPISchemaIfCond,
        features: List[QAPISchemaFeature],
        members: List[QAPISchemaEnumMember],
        prefix: Optional[str],
    ) -> None:
        assert name not in self.enums

        maxname = 0
        for member in members:
            enum_name = f"{name}{qapi_to_field_name_enum(member.name)}"
            maxname = max(maxname, len(enum_name))

        fields = ""
        for member in members:
            enum_name = f"{name}{qapi_to_field_name_enum(member.name)}"
            name2type = " " * (maxname - len(enum_name) + 1)
            fields += f"""\t{enum_name}{name2type}{name} = "{member.name}"\n"""

        self.enums[name] = TEMPLATE_ENUM.format(name=name, fields=fields[:-1])

    def visit_array_type(
        self,
        name: str,
        info: Optional[QAPISourceInfo],
        ifcond: QAPISchemaIfCond,
        element_type: QAPISchemaType,
    ) -> None:
        pass

    def visit_command(
        self,
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
        coroutine: bool,
    ) -> None:
        pass

    def visit_event(
        self,
        name: str,
        info: Optional[QAPISourceInfo],
        ifcond: QAPISchemaIfCond,
        features: List[QAPISchemaFeature],
        arg_type: Optional[QAPISchemaObjectType],
        boxed: bool,
    ) -> None:
        pass

    def write(self, output_dir: str) -> None:
        for module_name, content in self.target.items():
            go_module = module_name + "s.go"
            go_dir = "go"
            pathname = os.path.join(output_dir, go_dir, go_module)
            odir = os.path.dirname(pathname)
            os.makedirs(odir, exist_ok=True)

            with open(pathname, "w", encoding="utf8") as outfile:
                outfile.write(content)
