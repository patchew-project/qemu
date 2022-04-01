"""
Golang QAPI generator
"""
# Copyright (c) 2021 Red Hat Inc.
#
# Authors:
#  Victor Toso <victortoso@redhat.com>
# 
# This work is licensed under the terms of the GNU GPL, version 2.
# See the COPYING file in the top-level directory.

# Just for type hint on self
from __future__ import annotations

import os
from typing import Dict, List, Optional

from .schema import (
    QAPISchema,
    QAPISchemaVisitor,
    QAPISchemaEnumMember,
    QAPISchemaFeature,
    QAPISchemaIfCond,
    QAPISchemaObjectType,
    QAPISchemaObjectTypeMember,
    QAPISchemaVariants,
)
from .source import QAPISourceInfo

class QAPISchemaGenGolangVisitor(QAPISchemaVisitor):

    def __init__(self, prefix: str):
        super().__init__()
        self.target = {name: "" for name in ["enum"]}
        self.schema = None
        self._docmap = {}
        self.golang_package_name = "qapi"

    def visit_begin(self, schema):
        self.schema = schema

        # Every Go file needs to reference its package name
        for target in self.target:
            self.target[target] = f"package {self.golang_package_name}\n"

        # Iterate once in schema.docs to map doc objects to its name
        for doc in schema.docs:
            if doc.symbol is None:
                continue
            self._docmap[doc.symbol] = doc

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
        doc = self._docmap.get(name, None)
        doc_struct, doc_fields = qapi_to_golang_struct_docs(doc)

        value = qapi_to_field_name_enum(members[0].name)
        fields = f"\t{name}{value} {name} = iota\n"
        for member in members[1:]:
            field_doc = " " + doc_fields.get(member.name, "") if doc_fields else ""
            value = qapi_to_field_name_enum(member.name)
            fields += f"\t{name}{value}{field_doc}\n"

        self.target["enum"] += f'''
{doc_struct}
type {name} int32
const (
{fields[:-1]}
)
{generate_marshal_methods_enum(members)}
'''

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

            with open(pathname, "w") as outfile:
                outfile.write(content)


def gen_golang(schema: QAPISchema,
               output_dir: str,
               prefix: str) -> None:
    vis = QAPISchemaGenGolangVisitor(prefix)
    schema.visit(vis)
    vis.write(output_dir)

def generate_marshal_methods_enum(members: List[QAPISchemaEnumMember]) -> str:
    type = qapi_to_go_type_name(members[0].defined_in, "enum")

    marshal_switch_cases = ""
    unmarshal_switch_cases = ""
    for i in range(len(members)):
        go_type = type + qapi_to_field_name_enum(members[i].name)
        name = members[i].name

        marshal_switch_cases += f'''
    case {go_type}:
        return []byte(`"{name}"`), nil'''

        unmarshal_switch_cases += f'''
    case "{name}":
        (*s) = {go_type}'''

    return f'''
func (s {type}) MarshalJSON() ([]byte, error) {{
    switch s {{
{marshal_switch_cases[1:]}
    default:
        fmt.Println("Failed to decode {type}", s)
    }}
    return nil, errors.New("Failed")
}}

func (s *{type}) UnmarshalJSON(data []byte) error {{
    var name string

    if err := json.Unmarshal(data, &name); err != nil {{
        return err
    }}

    switch name {{
{unmarshal_switch_cases[1:]}
    default:
        fmt.Println("Failed to decode {type}", *s)
    }}
    return nil
}}
'''

# Takes the documentation object of a specific type and returns
# that type's documentation followed by its member's docs.
def qapi_to_golang_struct_docs(doc: QAPIDoc) -> (str, Dict[str, str]):
    if doc is None:
        return "// No documentation available", None

    main = ""
    if len(doc.body.text) > 0:
        main = f"// {doc.body.text}".replace("\n", "\n// ")

    for section in doc.sections:
        # Skip sections that are not useful for Golang consumers
        if section.name and "TODO" in section.name:
            continue

        # Small hack to only add // when doc.body.text was empty
        prefix = "// " if len(main) == 0 else "\n\n"
        main += f"{prefix}{section.name}: {section.text}".replace("\n", "\n// ")

    fields = {}
    for key, value in doc.args.items():
        if len(value.text) > 0:
            fields[key] = " // " + ' '.join(value.text.replace("\n", " ").split())

    return main, fields

def qapi_to_field_name_enum(name: str) -> str:
    return name.title().replace("-", "")

def qapi_to_go_type_name(name: str, meta: str) -> str:
    if name.startswith("q_obj_"):
        name = name[6:]

    # We want to keep CamelCase for Golang types. We want to avoid removing
    # already set CameCase names while fixing uppercase ones, eg:
    # 1) q_obj_SocketAddress_base -> SocketAddressBase
    # 2) q_obj_WATCHDOG-arg -> WatchdogArg
    words = [word for word in name.replace("_", "-").split("-")]
    name = words[0].title() if words[0].islower() or words[0].isupper() else words[0]
    name += ''.join(word.title() for word in words[1:])
    return name
