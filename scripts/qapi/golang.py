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
        self.target = {name: "" for name in ["alternate", "enum", "helper"]}
        self.objects_seen = {}
        self.schema = None
        self._docmap = {}
        self.golang_package_name = "qapi"

    def visit_begin(self, schema):
        self.schema = schema

        # Every Go file needs to reference its package name
        for target in self.target:
            self.target[target] = f"package {self.golang_package_name}\n"

        self.target["helper"] += f'''
    // Alias for go version lower than 1.18
    type Any = interface{{}}'''

        # Iterate once in schema.docs to map doc objects to its name
        for doc in schema.docs:
            if doc.symbol is None:
                continue
            self._docmap[doc.symbol] = doc

    def visit_end(self):
        self.schema = None

        self.target["helper"] += '''
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

        # Alternate marshal logic
        #
        # To avoid programming errors by users of this generated Go module,
        # we add a runtime check to error out in case the underlying Go type
        # doesn't not match any of supported types of the Alternate type.
        #
        # Also, Golang's json Marshal will include as JSON's object, the
        # wrapper we use to hold the Go struct (Value Any -> `Value: {...}`)
        # This would not be an valid QMP message so we workaround it by
        # calling RemoveValueObject function.
        doc = self._docmap.get(name, None)
        doc_struct, doc_fields = qapi_to_golang_struct_docs(doc)

        members_doc = '''// Options are:'''
        if_supported_types = ""
        for var in variants.variants:
            field_doc = doc_fields.get(var.name, "")
            field_go_type = qapi_schema_type_to_go_type(var.type.name)
            members_doc += f'''\n// * {var.name} ({field_go_type}):{field_doc[3:]}'''

            if field_go_type == "nil":
                field_go_type = "*string"

            if_supported_types += f'''typestr != "{field_go_type}" &&\n\t\t'''

        # Alternate unmarshal logic
        #
        # With Alternate types, we have to check the JSON data in order to
        # identify what is the target Go type. So, this is different than an
        # union which has an identifier that we can check first.
        # StrictDecode function tries to match the given JSON data to a given
        # Go type and it'll error in case it doesn´t fit, for instance, when
        # there were members in the JSON data that had no equivalent in the
        # target Go type.
        #
        # For this reason, the order is important.
        #
        # At this moment, the only field that must be checked first is JSON
        # NULL, which is relevant to a few alternate types. In the future, we
        # might need to improve the logic to be foolproof between target Go
        # types that might have a common base (non existing Today).
        check_type_str = '''
    // Check for {name}
    {{
        var value {go_type}
        if err := StrictDecode(&value, data); {error_check} {{
            s.Value = {set_value}
            return nil
        }}
    }}'''
        reference_checks = ""
        for var in variants.variants:
            if var.type.name == "null":
                # We use a pointer (by referece) to check for JSON's NULL
                reference_checks += check_type_str.format(
                        name = var.type.name,
                        go_type = "*string",
                        error_check = "err == nil && value == nil",
                        set_value = "nil")
                break;

        value_checks = ""
        for var in variants.variants:
            if var.type.name != "null":
                go_type = qapi_schema_type_to_go_type(var.type.name)
                value_checks += check_type_str.format(
                        name = var.type.name,
                        go_type = go_type,
                        error_check = "err == nil",
                        set_value = "value")

        unmarshal_checks = ""
        if len(reference_checks) > 0 and len(value_checks) > 0:
            unmarshal_checks = reference_checks[1:] + value_checks
        else:
            unmarshal_checks = reference_checks[1:] if len(reference_checks) > 0 else value_checks[1:]

        self.target["alternate"] += f'''
{doc_struct}
type {name} struct {{
{members_doc}
    Value Any
}}

func (s {name}) MarshalJSON() ([]byte, error) {{
    typestr := fmt.Sprintf("%T", s.Value)
    typestr = typestr[strings.LastIndex(typestr, ".")+1:]

    // Runtime check for supported types
    if typestr != "<nil>" &&
{if_supported_types[:-6]} {{
        return nil, errors.New(fmt.Sprintf("Type is not supported: %s", typestr))
    }}

    b, err := json.Marshal(s.Value);
    if err != nil {{
        return nil, err
    }}

    return b, nil
}}

func (s *{name}) UnmarshalJSON(data []byte) error {{
{unmarshal_checks}
    // Check type to error out nicely
    {{
        var value Any
        if err := json.Unmarshal(data, &value); err != nil {{
            return err
        }}
        return errors.New(fmt.Sprintf("Unsupported type %T (value: %v)", value, value))
    }}
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

def qapi_schema_type_to_go_type(type: str) -> str:
    schema_types_to_go = {'str': 'string', 'null': 'nil', 'bool': 'bool',
            'number': 'float64', 'size': 'uint64', 'int': 'int64', 'int8': 'int8',
            'int16': 'int16', 'int32': 'int32', 'int64': 'int64', 'uint8': 'uint8',
            'uint16': 'uint16', 'uint32': 'uint32', 'uint64': 'uint64',
            'any': 'Any', 'QType': 'QType',
    }

    prefix = ""
    if type.endswith("List"):
        prefix = "[]"
        type = type[:-4]

    type = schema_types_to_go.get(type, type)
    return prefix + type

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
