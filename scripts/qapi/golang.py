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
from typing import List, Optional, Tuple

from .schema import (
    QAPISchema,
    QAPISchemaAlternateType,
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

TEMPLATE_HELPER = """
// Creates a decoder that errors on unknown Fields
// Returns nil if successfully decoded @from payload to @into type
// Returns error if failed to decode @from payload to @into type
func StrictDecode(into interface{}, from []byte) error {
\tdec := json.NewDecoder(strings.NewReader(string(from)))
\tdec.DisallowUnknownFields()

\tif err := dec.Decode(into); err != nil {
\t\treturn err
\t}
\treturn nil
}

// This helper is used to move struct's fields into a map.
// This function is useful to merge JSON objects.
func unwrapToMap(m map[string]any, data any) error {
\tif bytes, err := json.Marshal(&data); err != nil {
\t\treturn fmt.Errorf("unwrapToMap: %s", err)
\t} else if err := json.Unmarshal(bytes, &m); err != nil {
\t\treturn fmt.Errorf("unwrapToMap: %s, data=%s", err, string(bytes))
\t}
\treturn nil
}
"""

TEMPLATE_ALTERNATE = """
// Only implemented on Alternate types that can take JSON NULL as value.
//
// This is a helper for the marshalling code. It should return true only when
// the Alternate is empty (no members are set), otherwise it returns false and
// the member set to be Marshalled.
type AbsentAlternate interface {
\tToAnyOrAbsent() (any, bool)
}
"""

TEMPLATE_ALTERNATE_NULLABLE_CHECK = """
\t\t}} else if s.{var_name} != nil {{
\t\t\treturn *s.{var_name}, false"""

TEMPLATE_ALTERNATE_MARSHAL_CHECK = """
\tif s.{var_name} != nil {{
\t\treturn json.Marshal(s.{var_name})
\t}} else """

TEMPLATE_ALTERNATE_UNMARSHAL_CHECK = """
\t// Check for {var_type}
\t{{
\t\ts.{var_name} = new({var_type})
\t\tif err := StrictDecode(s.{var_name}, data); err == nil {{
\t\t\treturn nil
\t\t}}
\t\ts.{var_name} = nil
\t}}
"""

TEMPLATE_ALTERNATE_NULLABLE = """
func (s *{name}) ToAnyOrAbsent() (any, bool) {{
\tif s != nil {{
\t\tif s.IsNull {{
\t\t\treturn nil, false
{absent_check_fields}
\t\t}}
\t}}

\treturn nil, true
}}
"""

TEMPLATE_ALTERNATE_METHODS = """
func (s {name}) MarshalJSON() ([]byte, error) {{
\t{marshal_check_fields}
\treturn {marshal_return_default}
}}

func (s *{name}) UnmarshalJSON(data []byte) error {{
{unmarshal_check_fields}
\treturn fmt.Errorf("Can't convert to {name}: %s", string(data))
}}
"""


TEMPLATE_STRUCT_WITH_NULLABLE_MARSHAL = """
func (s {type_name}) MarshalJSON() ([]byte, error) {{
\tm := make(map[string]any)
{map_members}{map_special}
\treturn json.Marshal(&m)
}}

func (s *{type_name}) UnmarshalJSON(data []byte) error {{
\ttmp := {struct}{{}}

\tif err := json.Unmarshal(data, &tmp); err != nil {{
\t\treturn err
\t}}

{set_members}{set_special}
\treturn nil
}}
"""


TEMPLATE_UNION_CHECK_VARIANT_FIELD = """
\tif s.{field} != nil && err == nil {{
\t\tif len(bytes) != 0 {{
\t\t\terr = errors.New(`multiple variant fields set`)
\t\t}} else if err = unwrapToMap(m, s.{field}); err == nil {{
\t\t\tm["{discriminator}"] = {go_enum_value}
\t\t\tbytes, err = json.Marshal(m)
\t\t}}
\t}}
"""

TEMPLATE_UNION_CHECK_UNBRANCHED_FIELD = """
\tif s.{field} && err == nil {{
\t\tif len(bytes) != 0 {{
\t\t\terr = errors.New(`multiple variant fields set`)
\t\t}} else {{
\t\t\tm["{discriminator}"] = {go_enum_value}
\t\t\tbytes, err = json.Marshal(m)
\t\t}}
\t}}
"""

TEMPLATE_UNION_DRIVER_VARIANT_CASE = """
\tcase {go_enum_value}:
\t\ts.{field} = new({member_type})
\t\tif err := json.Unmarshal(data, s.{field}); err != nil {{
\t\t\ts.{field} = nil
\t\t\treturn err
\t\t}}"""

TEMPLATE_UNION_DRIVER_UNBRANCHED_CASE = """
\tcase {go_enum_value}:
\t\ts.{field} = true
"""

TEMPLATE_UNION_METHODS = """
func (s {type_name}) MarshalJSON() ([]byte, error) {{
\tvar bytes []byte
\tvar err error
\tm := make(map[string]any)
\t{{
\t\ttype Alias {type_name}
\t\tv := Alias(s)
\t\tunwrapToMap(m, &v)
\t}}
{check_fields}
\tif err != nil {{
\t\treturn nil, fmt.Errorf("marshal {type_name} due:'%s' struct='%+v'", err, s)
\t}} else if len(bytes) == 0 {{
\t\treturn nil, fmt.Errorf("marshal {type_name} unsupported, struct='%+v'", s)
\t}}
\treturn bytes, nil
}}

func (s *{type_name}) UnmarshalJSON(data []byte) error {{
{base_type_def}
\ttmp := struct {{
\t\t{base_type_name}
\t}}{{}}

\tif err := json.Unmarshal(data, &tmp); err != nil {{
\t\treturn err
\t}}
{base_type_assign_unmarshal}
\tswitch tmp.{discriminator} {{
{driver_cases}
\tdefault:
\t\treturn fmt.Errorf("unmarshal {type_name} received unrecognized value '%s'",
\t\t\ttmp.{discriminator})
\t}}
\treturn nil
}}
"""

TEMPLATE_EVENT = """
type Timestamp struct {{
\tSeconds      int64 `json:"seconds"`
\tMicroseconds int64 `json:"microseconds"`
}}

type Event interface {{
\tGetName() string
\tGetTimestamp() Timestamp
}}

func MarshalEvent(e Event) ([]byte, error) {{
\tm := make(map[string]any)
\tm["event"] = e.GetName()
\tm["timestamp"] = e.GetTimestamp()
\tif bytes, err := json.Marshal(e); err != nil {{
\t\treturn []byte{{}}, err
\t}} else if len(bytes) > 2 {{
\t\tm["data"] = e
\t}}
\treturn json.Marshal(m)
}}

func UnmarshalEvent(data []byte) (Event, error) {{
\tbase := struct {{
\t\tName             string    `json:"event"`
\t\tMessageTimestamp Timestamp `json:"timestamp"`
\t}}{{}}
\tif err := json.Unmarshal(data, &base); err != nil {{
\t\treturn nil, fmt.Errorf("Failed to decode event: %s", string(data))
\t}}

\tswitch base.Name {{{cases}
\t}}
\treturn nil, errors.New("Failed to recognize event")
}}
"""

TEMPLATE_EVENT_METHODS = """
func (s *{type_name}) GetName() string {{
\treturn "{name}"
}}

func (s *{type_name}) GetTimestamp() Timestamp {{
\treturn s.MessageTimestamp
}}
"""


def gen_golang(schema: QAPISchema, output_dir: str, prefix: str) -> None:
    vis = QAPISchemaGenGolangVisitor(prefix)
    schema.visit(vis)
    vis.write(output_dir)


def qapi_name_is_base(name: str) -> bool:
    return qapi_name_is_object(name) and name.endswith("-base")


def qapi_name_is_object(name: str) -> bool:
    return name.startswith("q_obj_")


def qapi_to_field_name(name: str) -> str:
    return name.title().replace("_", "").replace("-", "")


def qapi_to_field_name_enum(name: str) -> str:
    return name.title().replace("-", "")


def qapi_to_go_type_name(name: str, meta: Optional[str] = None) -> str:
    if qapi_name_is_object(name):
        name = name[6:]

    # We want to keep CamelCase for Golang types. We want to avoid removing
    # already set CameCase names while fixing uppercase ones, eg:
    # 1) q_obj_SocketAddress_base -> SocketAddressBase
    # 2) q_obj_WATCHDOG-arg -> WatchdogArg
    words = list(name.replace("_", "-").split("-"))
    name = words[0]
    if name.islower() or name.isupper():
        name = name.title()

    name += "".join(word.title() for word in words[1:])

    types = ["event"]
    if meta in types:
        name = name[:-3] if name.endswith("Arg") else name
        name += meta.title().replace(" ", "")

    return name


def qapi_schema_type_to_go_type(qapitype: str) -> str:
    schema_types_to_go = {
        "str": "string",
        "null": "nil",
        "bool": "bool",
        "number": "float64",
        "size": "uint64",
        "int": "int64",
        "int8": "int8",
        "int16": "int16",
        "int32": "int32",
        "int64": "int64",
        "uint8": "uint8",
        "uint16": "uint16",
        "uint32": "uint32",
        "uint64": "uint64",
        "any": "any",
        "QType": "QType",
    }

    prefix = ""
    if qapitype.endswith("List"):
        prefix = "[]"
        qapitype = qapitype[:-4]

    qapitype = schema_types_to_go.get(qapitype, qapitype)
    return prefix + qapitype


def qapi_field_to_go_field(
    member_name: str, type_name: str
) -> Tuple[str, str, str]:
    # Nothing to generate on null types. We update some
    # variables to handle json-null on marshalling methods.
    if type_name == "null":
        return "IsNull", "bool", ""

    # This function is called on Alternate, so fields should be ptrs
    return (
        qapi_to_field_name(member_name),
        qapi_schema_type_to_go_type(type_name),
        "*",
    )


# Helper function for boxed or self contained structures.
def generate_struct_type(
    type_name, args: List[dict[str:str]] = None, ident: int = 0
) -> str:
    members = "{}"
    base_ident = "\t" * ident
    if args is not None:
        # Most of the logic below is to mimic the gofmt tool.
        # We calculate spaces between member and type and between
        # the type and tag.  Note that gofmt considers comments as
        # divider between ident blocks.
        maxname, maxtype = 0, 0
        blocks: tuple(int, int) = []
        for arg in args:
            if "comment" in arg:
                blocks.append((maxname, maxtype))
                maxname, maxtype = 0, 0
                continue

            if "type" not in arg:
                continue

            maxname = max(maxname, len(arg["name"]))
            maxtype = max(maxtype, len(arg["type"]))

        blocks.append((maxname, maxtype))
        block = 0

        maxname, maxtype = blocks[0]
        members = " {\n"
        for arg in args:
            if "comment" in arg:
                block += 1
                maxname, maxtype = blocks[block]
                members += f"""\t// {arg["comment"]}\n"""
                continue

            name2type = ""
            if "type" in arg:
                name2type = " " * (maxname - len(arg["name"]) + 1)
            type2tag = ""
            if "tag" in arg:
                type2tag = " " * (maxtype - len(arg["type"]) + 1)

            fident = "\t" * (ident + 1)
            gotype = "" if "type" not in arg else arg["type"]
            tag = "" if "tag" not in arg else arg["tag"]
            name = arg["name"]
            members += (
                f"""{fident}{name}{name2type}{gotype}{type2tag}{tag}\n"""
            )
        members += f"{base_ident}}}\n"

    with_type = f"\n{base_ident}type {type_name}" if len(type_name) > 0 else ""
    return f"""{with_type} struct{members}"""


def get_struct_field(
    self: QAPISchemaGenGolangVisitor,
    qapi_name: str,
    qapi_type_name: str,
    within_nullable_struct: bool,
    is_optional: bool,
    is_variant: bool,
) -> Tuple[dict[str:str], bool]:

    field = qapi_to_field_name(qapi_name)
    member_type = qapi_schema_type_to_go_type(qapi_type_name)
    is_nullable = False

    optional = ""
    if is_optional:
        if member_type in self.accept_null_types:
            is_nullable = True
        else:
            optional = ",omitempty"

    # Use pointer to type when field is optional
    isptr = "*" if is_optional and member_type[0] not in "*[" else ""

    if within_nullable_struct:
        # Within a struct which has a field of type that can hold JSON NULL,
        # we have to _not_ use a pointer, otherwise the Marshal methods are
        # not called.
        isptr = "" if member_type in self.accept_null_types else isptr

    fieldtag = (
        '`json:"-"`' if is_variant else f'`json:"{qapi_name}{optional}"`'
    )
    arg = {
        "name": f"{field}",
        "type": f"{isptr}{member_type}",
        "tag": f"{fieldtag}",
    }
    return arg, is_nullable


# This helper is used whithin a struct that has members that accept JSON NULL.
def map_and_set(
    is_nullable: bool, field: str, field_is_optional: bool, name: str
) -> Tuple[str, str]:

    mapstr = ""
    setstr = ""
    if is_nullable:
        mapstr = f"""
\tif val, absent := s.{field}.ToAnyOrAbsent(); !absent {{
\t\tm["{name}"] = val
\t}}
"""
        setstr += f"""
\tif _, absent := (&tmp.{field}).ToAnyOrAbsent(); !absent {{
\t\ts.{field} = &tmp.{field}
\t}}
"""
    elif field_is_optional:
        mapstr = f"""
\tif s.{field} != nil {{
\t\tm["{name}"] = s.{field}
\t}}
"""
        setstr = f"""\ts.{field} = tmp.{field}\n"""
    else:
        mapstr = f"""\tm["{name}"] = s.{field}\n"""
        setstr = f"""\ts.{field} = tmp.{field}\n"""

    return mapstr, setstr


def recursive_base_nullable(
    self: QAPISchemaGenGolangVisitor, base: Optional[QAPISchemaObjectType]
) -> Tuple[List[dict[str:str]], str, str, str, str]:
    fields: List[dict[str:str]] = []
    map_members = ""
    set_members = ""
    map_special = ""
    set_special = ""

    if not base:
        return fields, map_members, set_members, map_special, set_special

    if base.base is not None:
        embed_base = self.schema.lookup_entity(base.base.name)
        (
            fields,
            map_members,
            set_members,
            map_special,
            set_special,
        ) = recursive_base_nullable(self, embed_base)

    for member in base.local_members:
        field, _ = get_struct_field(
            self, member.name, member.type.name, True, member.optional, False
        )
        fields.append(field)

        member_type = qapi_schema_type_to_go_type(member.type.name)
        nullable = member_type in self.accept_null_types
        field_name = qapi_to_field_name(member.name)
        tomap, toset = map_and_set(
            nullable, field_name, member.optional, member.name
        )
        if nullable:
            map_special += tomap
            set_special += toset
        else:
            map_members += tomap
            set_members += toset

    return fields, map_members, set_members, map_special, set_special


# Helper function. This is executed when the QAPI schema has members
# that could accept JSON NULL (e.g: StrOrNull in QEMU"s QAPI schema).
# This struct will need to be extended with Marshal/Unmarshal methods to
# properly handle such atypical members.
#
# Only the Marshallaing methods are generated but we do need to iterate over
# all the members to properly set/check them in those methods.
def struct_with_nullable_generate_marshal(
    self: QAPISchemaGenGolangVisitor,
    name: str,
    base: Optional[QAPISchemaObjectType],
    members: List[QAPISchemaObjectTypeMember],
    variants: Optional[QAPISchemaVariants],
) -> str:

    (
        fields,
        map_members,
        set_members,
        map_special,
        set_special,
    ) = recursive_base_nullable(self, base)

    if members:
        for member in members:
            field, _ = get_struct_field(
                self,
                member.name,
                member.type.name,
                True,
                member.optional,
                False,
            )
            fields.append(field)

            member_type = qapi_schema_type_to_go_type(member.type.name)
            nullable = member_type in self.accept_null_types
            tomap, toset = map_and_set(
                nullable,
                qapi_to_field_name(member.name),
                member.optional,
                member.name,
            )
            if nullable:
                map_special += tomap
                set_special += toset
            else:
                map_members += tomap
                set_members += toset

    if variants:
        for variant in variants.variants:
            if variant.type.is_implicit():
                continue

            field, _ = get_struct_field(
                self,
                variant.name,
                variant.type.name,
                True,
                variant.optional,
                True,
            )
            fields.append(field)

            member_type = qapi_schema_type_to_go_type(variant.type.name)
            nullable = member_type in self.accept_null_types
            tomap, toset = map_and_set(
                nullable,
                qapi_to_field_name(variant.name),
                variant.optional,
                variant.name,
            )
            if nullable:
                map_special += tomap
                set_special += toset
            else:
                map_members += tomap
                set_members += toset

    type_name = qapi_to_go_type_name(name)
    struct = generate_struct_type("", fields, ident=1)[:-1]
    return TEMPLATE_STRUCT_WITH_NULLABLE_MARSHAL.format(
        struct=struct[1:],
        type_name=type_name,
        map_members=map_members,
        map_special=map_special,
        set_members=set_members,
        set_special=set_special,
    )


def recursive_base(
    self: QAPISchemaGenGolangVisitor,
    base: Optional[QAPISchemaObjectType],
    discriminator: Optional[str] = None,
) -> Tuple[List[dict[str:str]], bool]:
    fields: List[dict[str:str]] = []
    with_nullable = False

    if not base:
        return fields, with_nullable

    if base.base is not None:
        embed_base = self.schema.lookup_entity(base.base.name)
        fields, with_nullable = recursive_base(self, embed_base, discriminator)

    for member in base.local_members:
        if discriminator and member.name == discriminator:
            continue
        field, nullable = get_struct_field(
            self, member.name, member.type.name, False, member.optional, False
        )
        fields.append(field)
        with_nullable = True if nullable else with_nullable

    return fields, with_nullable


# Helper function that is used for most of QAPI types
def qapi_to_golang_struct(
    self: QAPISchemaGenGolangVisitor,
    name: str,
    info: Optional[QAPISourceInfo],
    __: QAPISchemaIfCond,
    ___: List[QAPISchemaFeature],
    base: Optional[QAPISchemaObjectType],
    members: List[QAPISchemaObjectTypeMember],
    variants: Optional[QAPISchemaVariants],
    ident: int = 0,
) -> str:

    discriminator = None if not variants else variants.tag_member.name
    fields, with_nullable = recursive_base(self, base, discriminator)
    if info.defn_meta == "event":
        fields.insert(
            0,
            {
                "name": "MessageTimestamp",
                "type": "Timestamp",
                "tag": """`json:"-"`""",
            },
        )

    if members:
        for member in members:
            field, nullable = get_struct_field(
                self,
                member.name,
                member.type.name,
                False,
                member.optional,
                False,
            )
            fields.append(field)
            with_nullable = True if nullable else with_nullable

    exists = {}
    if variants:
        fields.append({"comment": "Variants fields"})
        for variant in variants.variants:
            if variant.type.is_implicit():
                continue

            exists[variant.name] = True
            field, nullable = get_struct_field(
                self, variant.name, variant.type.name, False, True, True
            )
            fields.append(field)
            with_nullable = True if nullable else with_nullable

    if info.defn_meta == "union" and variants:
        enum_name = variants.tag_member.type.name
        enum_obj = self.schema.lookup_entity(enum_name)
        if len(exists) != len(enum_obj.members):
            fields.append({"comment": "Unbranched enum fields"})
            for member in enum_obj.members:
                if member.name in exists:
                    continue

                field, nullable = get_struct_field(
                    self, member.name, "bool", False, False, True
                )
                fields.append(field)
                with_nullable = True if nullable else with_nullable

    type_name = qapi_to_go_type_name(name, info.defn_meta)
    content = generate_struct_type(type_name, fields, ident)
    if with_nullable:
        content += struct_with_nullable_generate_marshal(
            self, name, base, members, variants
        )
    return content


def qapi_to_golang_methods_union(
    self: QAPISchemaGenGolangVisitor,
    name: str,
    base: Optional[QAPISchemaObjectType],
    variants: Optional[QAPISchemaVariants],
) -> str:

    type_name = qapi_to_go_type_name(name)

    assert base
    base_type_assign_unmarshal = ""
    base_type_name = qapi_to_go_type_name(base.name)
    base_type_def = qapi_to_golang_struct(
        self,
        base.name,
        base.info,
        base.ifcond,
        base.features,
        base.base,
        base.members,
        base.variants,
        ident=1,
    )

    discriminator = qapi_to_field_name(variants.tag_member.name)
    for member in base.local_members:
        field = qapi_to_field_name(member.name)
        if field == discriminator:
            continue
        base_type_assign_unmarshal += f"""
\ts.{field} = tmp.{field}"""

    driver_cases = ""
    check_fields = ""
    exists = {}
    enum_name = variants.tag_member.type.name
    if variants:
        for var in variants.variants:
            if var.type.is_implicit():
                continue

            field = qapi_to_field_name(var.name)
            enum_value = qapi_to_field_name_enum(var.name)
            member_type = qapi_schema_type_to_go_type(var.type.name)
            go_enum_value = f"""{enum_name}{enum_value}"""
            exists[go_enum_value] = True

            check_fields += TEMPLATE_UNION_CHECK_VARIANT_FIELD.format(
                field=field,
                discriminator=variants.tag_member.name,
                go_enum_value=go_enum_value,
            )
            driver_cases += TEMPLATE_UNION_DRIVER_VARIANT_CASE.format(
                go_enum_value=go_enum_value,
                field=field,
                member_type=member_type,
            )

    enum_obj = self.schema.lookup_entity(enum_name)
    if len(exists) != len(enum_obj.members):
        for member in enum_obj.members:
            value = qapi_to_field_name_enum(member.name)
            go_enum_value = f"""{enum_name}{value}"""

            if go_enum_value in exists:
                continue

            field = qapi_to_field_name(member.name)

            check_fields += TEMPLATE_UNION_CHECK_UNBRANCHED_FIELD.format(
                field=field,
                discriminator=variants.tag_member.name,
                go_enum_value=go_enum_value,
            )
            driver_cases += TEMPLATE_UNION_DRIVER_UNBRANCHED_CASE.format(
                go_enum_value=go_enum_value,
                field=field,
            )

    return TEMPLATE_UNION_METHODS.format(
        type_name=type_name,
        check_fields=check_fields[1:],
        base_type_def=base_type_def[1:],
        base_type_name=base_type_name,
        base_type_assign_unmarshal=base_type_assign_unmarshal,
        discriminator=discriminator,
        driver_cases=driver_cases[1:],
    )


def generate_template_alternate(
    self: QAPISchemaGenGolangVisitor,
    name: str,
    variants: Optional[QAPISchemaVariants],
) -> str:
    absent_check_fields = ""
    args: List[dict[str:str]] = []
    # to avoid having to check accept_null_types
    nullable = False
    if name in self.accept_null_types:
        # In QEMU QAPI schema, only StrOrNull and BlockdevRefOrNull.
        nullable = True
        marshal_return_default = """[]byte("{}"), nil"""
        marshal_check_fields = """if s.IsNull {
\t\treturn []byte("null"), nil
\t} else """
        unmarshal_check_fields = """
\t// Check for json-null first
\tif string(data) == "null" {
\t\ts.IsNull = true
\t\treturn nil
\t}"""
    else:
        marshal_return_default = f'nil, errors.New("{name} has empty fields")'
        marshal_check_fields = ""
        unmarshal_check_fields = f"""
\t// Check for json-null first
\tif string(data) == "null" {{
\t\treturn errors.New(`null not supported for {name}`)
\t}}"""

    if variants:
        for var in variants.variants:
            var_name, var_type, isptr = qapi_field_to_go_field(
                var.name, var.type.name
            )
            args.append(
                {
                    "name": f"{var_name}",
                    "type": f"{isptr}{var_type}",
                }
            )

            # Null is special, handled first
            if var.type.name == "null":
                assert nullable
                continue

            if nullable:
                absent_check_fields += (
                    TEMPLATE_ALTERNATE_NULLABLE_CHECK.format(var_name=var_name)
                )
            marshal_check_fields += TEMPLATE_ALTERNATE_MARSHAL_CHECK[
                2:
            ].format(var_name=var_name)
            unmarshal_check_fields += (
                TEMPLATE_ALTERNATE_UNMARSHAL_CHECK.format(
                    var_name=var_name, var_type=var_type
                )
            )

    content = generate_struct_type(name, args)
    if nullable:
        content += TEMPLATE_ALTERNATE_NULLABLE.format(
            name=name, absent_check_fields=absent_check_fields
        )
    content += TEMPLATE_ALTERNATE_METHODS.format(
        name=name,
        marshal_check_fields=marshal_check_fields[:-6],
        marshal_return_default=marshal_return_default,
        unmarshal_check_fields=unmarshal_check_fields[1:],
    )
    return content


def generate_template_event(events: dict[str, Tuple[str, str]]) -> str:
    cases = ""
    content = ""
    for name in sorted(events):
        case_type, gocode = events[name]
        content += gocode
        cases += f"""
\tcase "{name}":
\t\tevent := struct {{
\t\t\tData {case_type} `json:"data"`
\t\t}}{{}}

\t\tif err := json.Unmarshal(data, &event); err != nil {{
\t\t\treturn nil, fmt.Errorf("Failed to unmarshal: %s", string(data))
\t\t}}
\t\tevent.Data.MessageTimestamp = base.MessageTimestamp
\t\treturn &event.Data, nil
"""
    content += TEMPLATE_EVENT.format(cases=cases)
    return content


def generate_content_from_dict(data: dict[str, str]) -> str:
    content = ""

    for name in sorted(data):
        content += data[name]

    return content


class QAPISchemaGenGolangVisitor(QAPISchemaVisitor):
    # pylint: disable=too-many-arguments
    def __init__(self, _: str):
        super().__init__()
        types = (
            "alternate",
            "enum",
            "event",
            "helper",
            "struct",
            "union",
        )
        self.target = dict.fromkeys(types, "")
        self.schema: QAPISchema
        self.events: dict[str, Tuple[str, str]] = {}
        self.golang_package_name = "qapi"
        self.enums: dict[str, str] = {}
        self.alternates: dict[str, str] = {}
        self.structs: dict[str, str] = {}
        self.unions: dict[str, str] = {}
        self.accept_null_types = []

    def visit_begin(self, schema: QAPISchema) -> None:
        self.schema = schema

        # We need to be aware of any types that accept JSON NULL
        for name, entity in self.schema._entity_dict.items():
            if not isinstance(entity, QAPISchemaAlternateType):
                # Assume that only Alternate types accept JSON NULL
                continue

            for var in entity.variants.variants:
                if var.type.name == "null":
                    self.accept_null_types.append(name)
                    break

        # Every Go file needs to reference its package name
        # and most have some imports too.
        for target in self.target:
            self.target[target] = f"package {self.golang_package_name}\n"

            if target == "struct":
                imports = '\nimport "encoding/json"\n'
            elif target == "helper":
                imports = """\nimport (
\t"encoding/json"
\t"fmt"
\t"strings"
)
"""
            else:
                imports = """\nimport (
\t"encoding/json"
\t"errors"
\t"fmt"
)
"""
            if target != "enum":
                self.target[target] += imports

        self.target["helper"] += TEMPLATE_HELPER
        self.target["alternate"] += TEMPLATE_ALTERNATE

    def visit_end(self) -> None:
        del self.schema
        self.target["enum"] += generate_content_from_dict(self.enums)
        self.target["alternate"] += generate_content_from_dict(self.alternates)
        self.target["struct"] += generate_content_from_dict(self.structs)
        self.target["union"] += generate_content_from_dict(self.unions)
        self.target["event"] += generate_template_event(self.events)

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
        # Do not handle anything besides struct and unions.
        if (
            name == self.schema.the_empty_object_type.name
            or not isinstance(name, str)
            or info.defn_meta not in ["struct", "union"]
        ):
            return

        # Base structs are embed
        if qapi_name_is_base(name):
            return

        # visit all inner objects as well, they are not going to be
        # called by python's generator.
        if variants:
            for var in variants.variants:
                assert isinstance(var.type, QAPISchemaObjectType)
                self.visit_object_type(
                    self,
                    var.type.name,
                    var.type.info,
                    var.type.ifcond,
                    var.type.base,
                    var.type.local_members,
                    var.type.variants,
                )

        # Save generated Go code to be written later
        if info.defn_meta == "struct":
            assert name not in self.structs
            self.structs[name] = qapi_to_golang_struct(
                self, name, info, ifcond, features, base, members, variants
            )
        else:
            assert name not in self.unions
            self.unions[name] = qapi_to_golang_struct(
                self, name, info, ifcond, features, base, members, variants
            )
            self.unions[name] += qapi_to_golang_methods_union(
                self, name, base, variants
            )

    def visit_alternate_type(
        self,
        name: str,
        info: Optional[QAPISourceInfo],
        ifcond: QAPISchemaIfCond,
        features: List[QAPISchemaFeature],
        variants: QAPISchemaVariants,
    ) -> None:
        assert name not in self.alternates

        self.alternates[name] = generate_template_alternate(
            self, name, variants
        )

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
        assert name == info.defn_name
        assert name not in self.events
        type_name = qapi_to_go_type_name(name, info.defn_meta)

        if isinstance(arg_type, QAPISchemaObjectType):
            content = qapi_to_golang_struct(
                self,
                name,
                arg_type.info,
                arg_type.ifcond,
                arg_type.features,
                arg_type.base,
                arg_type.members,
                arg_type.variants,
            )
        else:
            args: List[dict[str:str]] = []
            args.append(
                {
                    "name": "MessageTimestamp",
                    "type": "Timestamp",
                    "tag": """`json:"-"`""",
                }
            )
            content = generate_struct_type(type_name, args)

        content += TEMPLATE_EVENT_METHODS.format(
            name=name, type_name=type_name
        )
        self.events[name] = (type_name, content)

    def write(self, output_dir: str) -> None:
        for module_name, content in self.target.items():
            go_module = module_name + "s.go"
            go_dir = "go"
            pathname = os.path.join(output_dir, go_dir, go_module)
            odir = os.path.dirname(pathname)
            os.makedirs(odir, exist_ok=True)

            with open(pathname, "w", encoding="utf8") as outfile:
                outfile.write(content)
