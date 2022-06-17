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

# Only variable is @unm_cases to handle
# all events's names and associated types.
TEMPLATE_EVENT = '''
type Timestamp struct {{
    Seconds      int64 `json:"seconds"`
    Microseconds int64 `json:"microseconds"`
}}

type Event interface {{
    GetName()      string
    GetTimestamp() Timestamp
}}

func MarshalEvent(e Event) ([]byte, error) {{
    baseStruct := struct {{
        Name           string    `json:"event"`
        EventTimestamp Timestamp `json:"timestamp"`
    }}{{
        Name:           e.GetName(),
        EventTimestamp: e.GetTimestamp(),
    }}
    base, err := json.Marshal(baseStruct)
    if err != nil {{
        return []byte{{}}, err
    }}

    dataStruct := struct {{
        Payload Event `json:"data"`
    }}{{
        Payload: e,
    }}
    data, err := json.Marshal(dataStruct)
    if err != nil {{
        return []byte{{}}, err
    }}

    if len(data) == len(`{{"data":{{}}}}`) {{
        return base, nil
    }}

    // Combines Event's base and data in a single JSON object
    result := fmt.Sprintf("%s,%s", base[:len(base)-1], data[1:])
    return []byte(result), nil
}}

func UnmarshalEvent(data []byte) (Event, error) {{
    base := struct {{
        Name           string    `json:"event"`
        EventTimestamp Timestamp `json:"timestamp"`
    }}{{}}
    if err := json.Unmarshal(data, &base); err != nil {{
        return nil, errors.New(fmt.Sprintf("Failed to decode event: %s", string(data)))
    }}

    switch base.Name {{
    {unm_cases}
    }}
    return nil, errors.New("Failed to recognize event")
}}
'''

# Only variable is @unm_cases to handle
# all command's names and associated types.
TEMPLATE_COMMAND = '''
type Command interface {{
    GetId()         string
    GetName()       string
    GetReturnType() CommandReturn
}}

func MarshalCommand(c Command) ([]byte, error) {{
    baseStruct := struct {{
        CommandId   string `json:"id,omitempty"`
        Name        string `json:"execute"`
    }}{{
        CommandId: c.GetId(),
        Name:      c.GetName(),
    }}
    base, err := json.Marshal(baseStruct)
    if err != nil {{
        return []byte{{}}, err
    }}

    argsStruct := struct {{
        Args Command `json:"arguments,omitempty"`
    }}{{
        Args: c,
    }}
    args, err := json.Marshal(argsStruct)
    if err != nil {{
        return []byte{{}}, err
    }}

    if len(args) == len(`{{"arguments":{{}}}}`) {{
        return base, nil
    }}

    // Combines Event's base and data in a single JSON object
    result := fmt.Sprintf("%s,%s", base[:len(base)-1], args[1:])
    return []byte(result), nil
}}

func UnmarshalCommand(data []byte) (Command, error) {{
    base := struct {{
        CommandId string `json:"id,omitempty"`
        Name      string `json:"execute"`
    }}{{}}
    if err := json.Unmarshal(data, &base); err != nil {{
        return nil, errors.New(fmt.Sprintf("Failed to decode command: %s", string(data)))
    }}

    switch base.Name {{
    {unm_cases}
    }}
    return nil, errors.New("Failed to recognize command")
}}
'''

TEMPLATE_COMMAND_RETURN = '''
type CommandReturn interface {
    GetId()          string
    GetCommandName() string
    GetError()       error
}

type EmptyCommandReturn struct {
    CommandId string          `json:"id,omitempty"`
    Error     *QapiError      `json:"error,omitempty"`
    Name      string          `json:"-"`
}

func (r EmptyCommandReturn) MarshalJSON() ([]byte, error) {
    return []byte(`{"return":{}}`), nil
}

func (r *EmptyCommandReturn) GetId() string {
    return r.CommandId
}

func (r *EmptyCommandReturn) GetCommandName() string {
    return r.Name
}

func (r *EmptyCommandReturn) GetError() error {
    return r.Error
}
'''

TEMPLATE_HELPER = '''
// Alias for go version lower than 1.18
type Any = interface{}

type QapiError struct {
    Class       string `json:"class"`
    Description string `json:"desc"`
}

func (err *QapiError) Error() string {
    return fmt.Sprintf("%s: %s", err.Class, err.Description)
}

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
        self.target = {name: "" for name in ["alternate", "command", "enum",
                                             "event", "helper", "struct",
                                             "union"]}
        self.objects_seen = {}
        self.schema = None
        self.events = {}
        self.commands = {}
        self.command_results = {}
        self.golang_package_name = "qapi"

    def visit_begin(self, schema):
        self.schema = schema

        # Every Go file needs to reference its package name
        for target in self.target:
            self.target[target] = f"package {self.golang_package_name}\n"

        self.target["helper"] += TEMPLATE_HELPER

    def visit_end(self):
        self.schema = None

        unm_cases = ""
        for name in sorted(self.events):
            case_type = self.events[name]
            unm_cases += f'''
    case "{name}":
        event := struct {{
            Data {case_type} `json:"data"`
        }}{{}}

        if err := json.Unmarshal(data, &event); err != nil {{
            return nil, errors.New(fmt.Sprintf("Failed to unmarshal: %s", string(data)))
        }}
        event.Data.EventTimestamp = base.EventTimestamp
        return &event.Data, nil
'''
        self.target["event"] += TEMPLATE_EVENT.format(unm_cases=unm_cases)

        unm_cases = ""
        for name in sorted(self.commands):
            case_type = self.commands[name]
            unm_cases += f'''
    case "{name}":
        command := struct {{
            Args {case_type} `json:"arguments"`
        }}{{}}

        if err := json.Unmarshal(data, &command); err != nil {{
            return nil, errors.New(fmt.Sprintf("Failed to unmarshal: %s", string(data)))
        }}
        command.Args.CommandId = base.CommandId
        return &command.Args, nil
'''
        self.target["command"] += TEMPLATE_COMMAND.format(unm_cases=unm_cases)

        self.target["command"] += TEMPLATE_COMMAND_RETURN

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

        # Base structs are embed
        if qapi_name_is_base(name):
            return

        # Safety checks.
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

        # Save generated Go code to be written later
        self.target[info.defn_meta] += qapi_to_golang_struct(name,
                                                             info,
                                                             ifcond,
                                                             features,
                                                             base,
                                                             members,
                                                             variants)
        if info.defn_meta == "union":
            self.target[info.defn_meta] += qapi_to_golang_methods_union(name,
                                                                        info,
                                                                        variants)

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
        # Safety check
        assert name == info.defn_name

        type_name = qapi_to_go_type_name(name, info.defn_meta)
        self.commands[name] = type_name
        command_ret = ""
        init_ret_type_name = f'''EmptyCommandReturn {{ Name: "{name}" }}'''
        if ret_type:
            cmd_ret_name = qapi_to_go_type_name(name, "command return")
            ret_type_name = qapi_schema_type_to_go_type(ret_type.name)
            init_ret_type_name = f'''{cmd_ret_name}{{}}'''
            isptr = "*" if ret_type_name[0] not in "*[" else ""
            self.command_results[name] = ret_type_name
            command_ret = f'''
type {cmd_ret_name} struct {{
    CommandId  string                `json:"id,omitempty"`
    Result    {isptr}{ret_type_name} `json:"return"`
    Error     *QapiError             `json:"error,omitempty"`
}}

func (r *{cmd_ret_name}) GetCommandName() string {{
    return "{name}"
}}

func (r *{cmd_ret_name}) GetId() string {{
    return r.CommandId
}}

func (r *{cmd_ret_name}) GetError() error {{
    return r.Error
}}
'''

        self_contained = True
        if arg_type and arg_type.name.startswith("q_obj"):
            self_contained = False

        content = ""
        if boxed or self_contained:
            args = "" if not arg_type else "\n" + arg_type.name
            args += '''\n\tCommandId   string `json:"-"`'''
            content = generate_struct_type(type_name, args)
        else:
            assert isinstance(arg_type, QAPISchemaObjectType)
            content = qapi_to_golang_struct(name,
                                            arg_type.info,
                                            arg_type.ifcond,
                                            arg_type.features,
                                            arg_type.base,
                                            arg_type.members,
                                            arg_type.variants)

        methods = f'''
func (c *{type_name}) GetName() string {{
    return "{name}"
}}

func (s *{type_name}) GetId() string {{
    return s.CommandId
}}

func (s *{type_name}) GetReturnType() CommandReturn {{
    return &{init_ret_type_name}
}}
'''
        self.target["command"] += content + methods + command_ret

    def visit_event(self, name, info, ifcond, features, arg_type, boxed):
        assert name == info.defn_name
        type_name = qapi_to_go_type_name(name, info.defn_meta)
        self.events[name] = type_name

        self_contained = True
        if arg_type and arg_type.name.startswith("q_obj"):
            self_contained = False

        content = ""
        if self_contained:
            content = generate_struct_type(type_name, '''EventTimestamp Timestamp `json:"-"`''')
        else:
            assert isinstance(arg_type, QAPISchemaObjectType)
            content = qapi_to_golang_struct(name,
                                            arg_type.info,
                                            arg_type.ifcond,
                                            arg_type.features,
                                            arg_type.base,
                                            arg_type.members,
                                            arg_type.variants)

        methods = f'''
func (s *{type_name}) GetName() string {{
    return "{name}"
}}

func (s *{type_name}) GetTimestamp() Timestamp {{
    return s.EventTimestamp
}}
'''
        self.target["event"] += content + methods

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


# Helper function that is used for most of QAPI types
def qapi_to_golang_struct(name: str,
                          info: Optional[QAPISourceInfo],
                          ifcond: QAPISchemaIfCond,
                          features: List[QAPISchemaFeature],
                          base: Optional[QAPISchemaObjectType],
                          members: List[QAPISchemaObjectTypeMember],
                          variants: Optional[QAPISchemaVariants]) -> str:

    type_name = qapi_to_go_type_name(name, info.defn_meta)

    fields = ""
    if info.defn_meta == "event":
        fields += '''\tEventTimestamp Timestamp `json:"-"`\n'''
    elif info.defn_meta == "command":
        fields += '''\tCommandId string `json:"-"`\n'''

    if base:
        base_fields = ""
        for lm in base.local_members:
            # We do not include the Union's discriminator
            # into the generated Go struct as the selection
            # of what variant was set is enough on Go side.
            if variants and variants.tag_member.name == lm.name:
                continue

            field = qapi_to_field_name(lm.name)
            member_type = qapi_schema_type_to_go_type(lm.type.name)

            isptr = "*" if lm.optional and member_type[0] not in "*[" else ""
            optional = ",omitempty" if lm.optional else ""
            fieldtag = f'`json:"{lm.name}{optional}"`'

            base_fields += f"\t{field} {isptr}{member_type}{fieldtag}\n"

        if len(base_fields) > 0:
            fields += f"\t// Base fields\n\t{base_fields}\n"

    if members:
        for memb in members:
            field = qapi_to_field_name(memb.name)
            member_type = qapi_schema_type_to_go_type(memb.type.name)

            isptr = "*" if memb.optional and member_type[0] not in "*[" else ""
            optional = ",omitempty" if memb.optional else ""
            fieldtag = f'`json:"{memb.name}{optional}"`'

            fields += f"\t{field} {isptr}{member_type}{fieldtag}\n"

        fields += "\n"

    if variants:
        fields += "\t// Variants fields\n"
        for var in variants.variants:
            if var.type.is_implicit():
                continue

            field = qapi_to_field_name(var.name)
            member_type = qapi_schema_type_to_go_type(var.type.name)
            # Variant's are handled in the Marshal/Unmarshal methods
            fieldtag = '`json:"-"`'
            fields += f"\t{field} *{member_type}{fieldtag}\n"

    return generate_struct_type(type_name, fields)


def qapi_to_golang_methods_union(name: str,
                                 info: Optional[QAPISourceInfo],
                                 variants: Optional[QAPISchemaVariants]
                                 ) -> str:

    type_name = qapi_to_go_type_name(name, info.defn_meta)

    driverCases = ""
    checkFields = ""
    if variants:
        for var in variants.variants:
            if var.type.is_implicit():
                continue

            field = qapi_to_field_name(var.name)
            member_type = qapi_schema_type_to_go_type(var.type.name)

            if len(checkFields) > 0:
                checkFields += "\t} else "
            checkFields += f'''if s.{field} != nil {{
        driver = "{var.name}"
        payload, err = json.Marshal(s.{field})
'''
            # for Unmarshal method
            driverCases += f'''
    case "{var.name}":
        s.{field} = new({member_type})
        if err := json.Unmarshal(data, s.{field}); err != nil {{
            s.{field} = nil
            return err
        }}'''

        checkFields += "}"

    return f'''
func (s {type_name}) MarshalJSON() ([]byte, error) {{
    type Alias {type_name}
    alias := Alias(s)
    base, err := json.Marshal(alias)
    if err != nil {{
        return nil, err
    }}

    driver := ""
    payload := []byte{{}}
    {checkFields}

    if err != nil {{
        return nil, err
    }}

    if len(base) == len("{{}}") {{
        base = []byte("")
    }} else {{
        base = append([]byte{{','}}, base[1:len(base)-1]...)
    }}

    if len(payload) == 0 || len(payload) == len("{{}}") {{
        payload = []byte("")
    }} else {{
        payload = append([]byte{{','}}, payload[1:len(payload)-1]...)
    }}

    result := fmt.Sprintf(`{{"{variants.tag_member.name}":"%s"%s%s}}`, driver, base, payload)
    return []byte(result), nil
}}

func (s *{type_name}) UnmarshalJSON(data []byte) error {{
    type Alias {type_name}
    peek := struct {{
        Alias
        Driver string `json:"{variants.tag_member.name}"`
    }}{{}}


    if err := json.Unmarshal(data, &peek); err != nil {{
        return err
    }}
    *s = {type_name}(peek.Alias)

    switch peek.Driver {{
    {driverCases}
    }}
    // Unrecognizer drivers are silently ignored.
    return nil
}}
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


def qapi_name_is_base(name: str) -> bool:
    return name.startswith("q_obj_") and name.endswith("-base")


def qapi_to_go_type_name(name: str, meta: str) -> str:
    if name.startswith("q_obj_"):
        name = name[6:]

    # We want to keep CamelCase for Golang types. We want to avoid removing
    # already set CameCase names while fixing uppercase ones, eg:
    # 1) q_obj_SocketAddress_base -> SocketAddressBase
    # 2) q_obj_WATCHDOG-arg -> WatchdogArg
    words = [word for word in name.replace("_", "-").split("-")]
    name = words[0]
    if name.islower() or name.isupper():
        name = name.title()

    name += ''.join(word.title() for word in words[1:])

    if meta in ["event", "command", "command return"]:
        name = name[:-3] if name.endswith("Arg") else name
        name += meta.title().replace(" ", "")

    return name
