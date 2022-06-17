"""
Golang QAPI generator
"""
# Copyright (c) 2022 Red Hat Inc.
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


TEMPLATE_HELPER = '''
// Alias for go version lower than 1.18
type Any = interface{}

// Creates a decoder that errors on unknown Fields
// Returns true if successfully decoded @from string @into type
// Returns false without error is failed with "unknown field"
// Returns false with error is a different error was found
func StrictDecode(into interface{}, from []byte) error {
    dec := json.NewDecoder(strings.NewReader(string(from)))
    dec.DisallowUnknownFields()

    if err := dec.Decode(into); err != nil {
        return err
    }
    return nil
}
'''


class QAPISchemaGenGolangVisitor(QAPISchemaVisitor):

    def __init__(self, prefix: str):
        super().__init__()
        self.target = {name: "" for name in ["alternate", "enum", "helper"]}
        self.objects_seen = {}
        self.schema = None
        self.golang_package_name = "qapi"

    def visit_begin(self, schema):
        self.schema = schema

        # Every Go file needs to reference its package name
        for target in self.target:
            self.target[target] = f"package {self.golang_package_name}\n"

        self.target["helper"] += TEMPLATE_HELPER

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
        assert name not in self.objects_seen
        self.objects_seen[name] = True

        marshal_return_default = f'nil, errors.New("{name} has empty fields")'
        marshal_check_fields = ""
        unmarshal_check_fields = ""
        variant_fields = ""

        # We need to check if the Alternate type supports NULL as that
        # means that JSON to Go would allow all fields to be empty.
        # Alternate that don't support NULL, would fail to convert
        # to JSON if all fields were empty.
        return_on_null = f"errors.New(`null not supported for {name}`)"

        # Assembly the fields and all the checks for Marshal and
        # Unmarshal methods
        for var in variants.variants:
            # Nothing to generate on null types. We update some
            # variables to handle json-null on marshalling methods.
            if var.type.name == "null":
                marshal_return_default = '[]byte("null"), nil'
                return_on_null = "nil"
                continue

            var_name = qapi_to_field_name(var.name)
            var_type = qapi_schema_type_to_go_type(var.type.name)
            variant_fields += f"\t{var_name} *{var_type}\n"

            if len(marshal_check_fields) > 0:
                marshal_check_fields += "} else "

            marshal_check_fields += f'''if s.{var_name} != nil {{
        return json.Marshal(s.{var_name})
    '''

            unmarshal_check_fields += f'''// Check for {var_type}
        {{
            s.{var_name} = new({var_type})
            if err := StrictDecode(s.{var_name}, data); err == nil {{
                return nil
            }}
            s.{var_name} = nil
        }}
'''

        marshal_check_fields += "}"

        self.target["alternate"] += generate_struct_type(name, variant_fields)
        self.target["alternate"] += f'''
func (s {name}) MarshalJSON() ([]byte, error) {{
    {marshal_check_fields}
    return {marshal_return_default}
}}

func (s *{name}) UnmarshalJSON(data []byte) error {{
    // Check for json-null first
    if string(data) == "null" {{
        return {return_on_null}
    }}
    {unmarshal_check_fields}
    return errors.New(fmt.Sprintf("Can't convert to {name}: %s", string(data)))
}}
'''

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

        self.target["enum"] += f'''
type {name} string
const (
{fields[:-1]}
)
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


# Helper function for boxed or self contained structures.
def generate_struct_type(type_name, args="") -> str:
    args = args if len(args) == 0 else f"\n{args}\n"
    return f'''
type {type_name} struct {{{args}}}
'''


def qapi_schema_type_to_go_type(type: str) -> str:
    schema_types_to_go = {
            'str': 'string', 'null': 'nil', 'bool': 'bool', 'number':
            'float64', 'size': 'uint64', 'int': 'int64', 'int8': 'int8',
            'int16': 'int16', 'int32': 'int32', 'int64': 'int64', 'uint8':
            'uint8', 'uint16': 'uint16', 'uint32': 'uint32', 'uint64':
            'uint64', 'any': 'Any', 'QType': 'QType',
    }

    prefix = ""
    if type.endswith("List"):
        prefix = "[]"
        type = type[:-4]

    type = schema_types_to_go.get(type, type)
    return prefix + type


def qapi_to_field_name_enum(name: str) -> str:
    return name.title().replace("-", "")


def qapi_to_field_name(name: str) -> str:
    return name.title().replace("_", "").replace("-", "")
