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
        self.target = {name: "" for name in ["alternate", "command", "enum", "event", "helper", "struct", "union"]}
        self.objects_seen = {}
        self.schema = None
        self.events = {}
        self.commands = {}
        self.command_results = {}
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

        # EventBase and Event are not specified in the QAPI schema,
        # so we need to generate it ourselves.
        self.target["event"] += '''
type EventBase struct {
    Name      string `json:"event"`
    Timestamp struct {
        Seconds      int64 `json:"seconds"`
        Microseconds int64 `json:"microseconds"`
    } `json:"timestamp"`
}

type Event struct {
    EventBase
    Arg       Any    `json:"data,omitempty"`
}
'''
        self.target["event"] += generate_marshal_methods('Event', self.events)

        self.target["command"] += '''
type CommandBase struct {
    Id   string `json:"id,omitempty"`
    Name string `json:"execute"`
}

type Command struct {
    CommandBase
    Arg Any    `json:"arguments,omitempty"`
}
'''
        self.target["command"] += generate_marshal_methods('Command', self.commands)

        self.target["command"] += '''
type CommandResult struct {
	CommandBase
	Value       Any `json:"return,omitempty"`
}

func (s Command) GetReturnType() CommandResult {
	return CommandResult{
		CommandBase: s.CommandBase,
	}
}

// In order to evaluate nil value to empty JSON object
func (s *CommandResult) MarshalJSON() ([]byte, error) {
	if s.Value == nil {
		return []byte(`{"return":{}}`), nil
	}
	tmp := struct {
		Value Any `json:"return"`
	}{Value: s.Value}

	return json.Marshal(&tmp)
}
'''
        self.target["command"] += generate_marshal_methods('CommandResult', self.command_results)

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
        # Do not handle anything besides struct and unions.
        if (name == self.schema.the_empty_object_type.name or
                not isinstance(name, str) or
                info.defn_meta not in ["struct", "union"]):
            return

        assert name not in self.objects_seen
        self.objects_seen[name] = True

        # visit all inner objects as well, they are not going to be
        # called by python's generator.
        if variants:
            for var in variants.variants:
                assert isinstance(var.type, QAPISchemaObjectType)
                self.visit_object_type(self,
                                       var.type.name,
                                       var.type.info,
                                       var.type.ifcond,
                                       var.type.base,
                                       var.type.local_members,
                                       var.type.variants)

        doc = self._docmap.get(info.defn_name, None)
        self.target[info.defn_meta] += qapi_to_golang_struct(name, doc, info,
                ifcond, features, base, members, variants)

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
        # TLDR: We don't need to any extra boilerplate in Go to handle Arrays.
        #
        # This function is implemented just to be sure that:
        # 1. Every array type ends with List
        # 2. Every array type's element is the array type without 'List'
        assert name.endswith("List") and name[:-4] == element_type.name

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
        assert name == info.defn_name
        type_name = qapi_to_go_type_name(name, info.defn_meta)
        self.commands[name] = type_name
        if ret_type:
            ret_type_name = qapi_schema_type_to_go_type(ret_type.name)
            self.command_results[name] = ret_type_name

        doc = self._docmap.get(name, None)
        self_contained = True if not arg_type or not arg_type.name.startswith("q_obj") else False
        content = ""
        if boxed or self_contained:
            args = "" if not arg_type else "\n" + arg_type.name
            doc_struct, _ = qapi_to_golang_struct_docs(doc)
            content = generate_struct_type(type_name, args, doc_struct)
        else:
            assert isinstance(arg_type, QAPISchemaObjectType)
            content = qapi_to_golang_struct(name,
                                            doc,
                                            arg_type.info,
                                            arg_type.ifcond,
                                            arg_type.features,
                                            arg_type.base,
                                            arg_type.members,
                                            arg_type.variants)

        self.target["command"] += content

    def visit_event(self, name, info, ifcond, features, arg_type, boxed):
        assert name == info.defn_name
        type_name = qapi_to_go_type_name(name, info.defn_meta)
        self.events[name] = type_name

        doc = self._docmap.get(name, None)
        self_contained = True if not arg_type or not arg_type.name.startswith("q_obj") else False
        content = ""
        if self_contained:
            doc_struct, _ = qapi_to_golang_struct_docs(doc)
            content = generate_struct_type(type_name, "", doc_struct)
        else:
            assert isinstance(arg_type, QAPISchemaObjectType)
            content = qapi_to_golang_struct(name,
                                            doc,
                                            arg_type.info,
                                            arg_type.ifcond,
                                            arg_type.features,
                                            arg_type.base,
                                            arg_type.members,
                                            arg_type.variants)

        self.target["event"] += content

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
def generate_struct_type(type_name, args="", doc_struct="") -> str:
    args =  args if len(args) == 0 else f"\n{args}\n"
    return f'''
{doc_struct}
type {type_name} struct {{{args}}}
'''

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

# Marshal methods for Event, Commad and Union types
def generate_marshal_methods(type: str,
                             type_dict: Dict[str, str],
                             discriminator: str = "",
                             base: str = "") -> str:
    type_is_union = False
    json_field = ""
    struct_field = ""
    if type == "Event":
        base = type + "Base"
        discriminator = "base.Name"
        struct_field = "Arg"
        json_field = "data"
    elif type == "Command":
        base = type + "Base"
        discriminator = "base.Name"
        struct_field = "Arg"
        json_field = "arguments"
    elif type == "CommandResult":
        base = "CommandBase"
        discriminator = "s.Name"
        struct_field = "Value"
        json_field = "return"
    else:
        assert base != ""
        discriminator = "base." + discriminator
        type_is_union = True

    switch_case_format = ""
    if not type_is_union:
        switch_case_format = '''
    case "{name}":
        tmp := struct {{
            Value {isptr}{case_type} `json:"{json_field},omitempty"`
        }}{{}}
        if err := json.Unmarshal(data, &tmp); err != nil {{
            return err
        }}
        if tmp.Value == nil {{
            s.{struct_field} = nil
        }} else {{
            s.{struct_field} = {isptr}tmp.Value
        }}'''
    else:
        switch_case_format = '''
    case {name}:
        value := {case_type}{{}}
        if err := json.Unmarshal(data, &value); err != nil {{
            return err
        }}
        s.Value = value'''

    if_supported_types = ""
    added = {}
    switch_cases = ""
    for name in sorted(type_dict):
        case_type = type_dict[name]
        isptr = "*" if case_type[0] not in "*[" else ""
        switch_cases += switch_case_format.format(name = name,
                                                  struct_field = struct_field,
                                                  json_field = json_field,
                                                  isptr = isptr,
                                                  case_type = case_type)
        if case_type not in added:
            if_supported_types += f'''typestr != "{case_type}" &&\n\t\t'''
            added[case_type] = True

    marshalfn = ""
    if type_is_union:
        marshalfn = f'''
func (s {type}) MarshalJSON() ([]byte, error) {{
	base, err := json.Marshal(s.{base})
	if err != nil {{
		return nil, err
	}}

    typestr := fmt.Sprintf("%T", s.Value)
    typestr = typestr[strings.LastIndex(typestr, ".")+1:]

    // "The branches need not cover all possible enum values"
    // This means that on Marshal, we can safely ignore empty values
    if typestr == "<nil>" {{
        return []byte(base), nil
    }}

    // Runtime check for supported value types
    if {if_supported_types[:-6]} {{
        return nil, errors.New(fmt.Sprintf("Type is not supported: %s", typestr))
    }}
	value, err := json.Marshal(s.Value)
	if err != nil {{
		return nil, err
	}}

    // Workaround to avoid checking s.Value being empty
    if string(value) == "{{}}" {{
        return []byte(base), nil
    }}

    // Removes the last '}}' from base and the first '{{' from value, in order to
    // return a single JSON object.
    result := fmt.Sprintf("%s,%s", base[:len(base)-1], value[1:])
    return []byte(result), nil
}}
'''
    unmarshal_base = ""
    unmarshal_default_warn = ""
    if type != "CommandResult":
        unmarshal_base = f'''
    var base {base}
    if err := json.Unmarshal(data, &base); err != nil {{
        return err
    }}
    s.{base} = base
'''
        unmarshal_default_warn = f'''
    default:
        fmt.Println("Failed to decode {type}", {discriminator})'''

    return f'''{marshalfn}
func (s *{type}) UnmarshalJSON(data []byte) error {{
    {unmarshal_base}
    switch {discriminator} {{
{switch_cases[1:]}
    {unmarshal_default_warn}
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

# Helper function that is used for most of QAPI types
def qapi_to_golang_struct(name: str,
                          doc: QAPIDoc,
                          info: Optional[QAPISourceInfo],
                          ifcond: QAPISchemaIfCond,
                          features: List[QAPISchemaFeature],
                          base: Optional[QAPISchemaObjectType],
                          members: List[QAPISchemaObjectTypeMember],
                          variants: Optional[QAPISchemaVariants]) -> str:

    type_name = qapi_to_go_type_name(name, info.defn_meta)
    doc_struct, doc_fields = qapi_to_golang_struct_docs(doc)

    base_fields = ""
    if base:
        base_type_name = qapi_to_go_type_name(base.name, base.meta)
        base_fields = f"\t// Base type for this struct\n\t{base_type_name}\n"

    own_fields = ""
    for memb in members:
        field = qapi_to_field_name(memb.name)
        member_type = qapi_schema_type_to_go_type(memb.type.name)

        # In Golang, we are using "encoding/json" library to Marshal and Unmarshal between
        # over-the-wire QMP and Golang struct. The field tag 'omitempty' does not behave as
        # expected for some types with default values and they only way to "ignore by default"
        # unset fields is to have them as reference in the Struct.
        # This way, a *bool and *int can be ignored where a bool or int might have been set.
        isptr = "*" if memb.optional and member_type[0] not in "*[" else ""
        optional = ",omitempty" if memb.optional else ""
        fieldtag = '`json:"{name}{optional}"`'.format(name=memb.name,optional=optional)

        field_doc = doc_fields.get(memb.name, "")
        own_fields += f"\t{field} {isptr}{member_type}{fieldtag}{field_doc}\n"

    union_types = {}
    variant_fields = ""
    if variants:
        variant_fields = f"// Value based on @{variants.tag_member.name}, possible types:"
        for var in variants.variants:
            if var.type.is_implicit():
                continue

            name = variants.tag_member._type_name + var.name.title().replace("-", "")
            union_types[name] = var.type.name
            variant_fields += f"\n\t// * {var.type.c_unboxed_type()}"

        variant_fields += f"\n\tValue Any"

    all_fields = base_fields if len(base_fields) > 0 else ""
    all_fields += own_fields[:-1] if len(own_fields) > 0 else ""
    all_fields += variant_fields if len(variant_fields) > 0 else ""

    unmarshal_fn = ""
    if info.defn_meta == "union" and variants is not None:
        # Union's without variants are the Union's base data structure.
        # e.g: SchemaInfo's base is SchemainfoBase.
        discriminator = qapi_to_field_name(variants.tag_member.name)
        base = qapi_to_go_type_name(variants.tag_member.defined_in,
                                    variants.tag_member.info.defn_meta)
        unmarshal_fn = generate_marshal_methods(type_name,
                                                union_types,
                                                discriminator = discriminator,
                                                base = base_type_name)

    return generate_struct_type(type_name, all_fields, doc_struct) + unmarshal_fn

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

def qapi_to_field_name(name: str) -> str:
    return name.title().replace("_", "").replace("-", "")

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

    if meta == "event" or meta == "command":
        name = name[:-3] if name.endswith("Arg") else name
        name += meta.title()

    return name
