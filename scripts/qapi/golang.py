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


def qapi_to_go_type_name(name: str) -> str:
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
    is_optional: bool,
    is_variant: bool,
) -> dict[str:str]:

    field = qapi_to_field_name(qapi_name)
    member_type = qapi_schema_type_to_go_type(qapi_type_name)

    optional = ""
    if is_optional:
        if member_type not in self.accept_null_types:
            optional = ",omitempty"

    # Use pointer to type when field is optional
    isptr = "*" if is_optional and member_type[0] not in "*[" else ""

    fieldtag = (
        '`json:"-"`' if is_variant else f'`json:"{qapi_name}{optional}"`'
    )
    arg = {
        "name": f"{field}",
        "type": f"{isptr}{member_type}",
        "tag": f"{fieldtag}",
    }
    return arg


def recursive_base(
    self: QAPISchemaGenGolangVisitor,
    base: Optional[QAPISchemaObjectType],
    discriminator: Optional[str] = None,
) -> List[dict[str:str]]:
    fields: List[dict[str:str]] = []

    if not base:
        return fields

    if base.base is not None:
        embed_base = self.schema.lookup_entity(base.base.name)
        fields = recursive_base(self, embed_base, discriminator)

    for member in base.local_members:
        if discriminator and member.name == discriminator:
            continue
        field = get_struct_field(
            self, member.name, member.type.name, member.optional, False
        )
        fields.append(field)

    return fields


# Helper function that is used for most of QAPI types
def qapi_to_golang_struct(
    self: QAPISchemaGenGolangVisitor,
    name: str,
    _: Optional[QAPISourceInfo],
    __: QAPISchemaIfCond,
    ___: List[QAPISchemaFeature],
    base: Optional[QAPISchemaObjectType],
    members: List[QAPISchemaObjectTypeMember],
    variants: Optional[QAPISchemaVariants],
) -> str:

    fields = recursive_base(self, base)

    if members:
        for member in members:
            field = get_struct_field(
                self, member.name, member.type.name, member.optional, False
            )
            fields.append(field)

    if variants:
        fields.append({"comment": "Variants fields"})
        for variant in variants.variants:
            if variant.type.is_implicit():
                continue

            field = get_struct_field(
                self, variant.name, variant.type.name, True, True
            )
            fields.append(field)

    type_name = qapi_to_go_type_name(name)
    content = generate_struct_type(type_name, fields)
    return content


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
            "helper",
            "struct",
        )
        self.target = dict.fromkeys(types, "")
        self.schema: QAPISchema
        self.golang_package_name = "qapi"
        self.enums: dict[str, str] = {}
        self.alternates: dict[str, str] = {}
        self.structs: dict[str, str] = {}
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

            if target == "helper":
                imports = """\nimport (
\t"encoding/json"
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
        # Do not handle anything besides struct.
        if (
            name == self.schema.the_empty_object_type.name
            or not isinstance(name, str)
            or info.defn_meta not in ["struct"]
        ):
            return

        # Base structs are embed
        if qapi_name_is_base(name):
            return

        # Safety checks.
        assert name not in self.structs

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
        self.structs[name] = qapi_to_golang_struct(
            self, name, info, ifcond, features, base, members, variants
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
