# SPDX-License-Identifier: GPL-2.0-or-later
"""
Collect introspectable types from a QAPI schema and assign masked names.

Copyright (C) 2015-2026 Red Hat, Inc.

Authors:
 Markus Armbruster <armbru@redhat.com>
 John Snow <jsnow@redhat.com>
 Marc-André Lureau <marcandre.lureau@redhat.com>
"""

from typing import (
    Dict,
    List,
    Optional,
    Sequence,
    Set,
)

from .schema import (
    QAPISchema,
    QAPISchemaAlternatives,
    QAPISchemaArrayType,
    QAPISchemaBranches,
    QAPISchemaBuiltinType,
    QAPISchemaEntity,
    QAPISchemaFeature,
    QAPISchemaIfCond,
    QAPISchemaObjectType,
    QAPISchemaObjectTypeMember,
    QAPISchemaType,
    QAPISchemaVisitor,
)
from .source import QAPISourceInfo


class QAPISchemaTypeAnalysis(QAPISchemaVisitor):
    """Analyze types from a QAPI schema.

    Every non-builtin, non-array type is given a masked introspection
    name (an integer string).
    """

    def __init__(self, unmask: bool):
        self._unmask = unmask
        self._schema: Optional[QAPISchema] = None
        # Ordered list + set: insert during iteration + O(1) check
        self._types: List[QAPISchemaType] = []
        self._types_set: Set[QAPISchemaType] = set()
        self._name_map: Dict[str, str] = {}

    def visit_begin(self, schema: QAPISchema) -> None:
        self._schema = schema
        self._types = []
        self._types_set = set()
        self._name_map = {}

    def visit_end(self) -> None:
        assert self._schema is not None
        # Discover type dependencies; the list grows as
        # visiting each type registers the types it references.
        for typ in self._types:
            typ.visit(self)

        # Assign masked names now that all introspected types are known.
        counter = 0
        for typ in self._types:
            if isinstance(typ, (QAPISchemaBuiltinType, QAPISchemaArrayType)):
                continue
            self._name_map[typ.name] = (
                typ.name if self._unmask else str(counter))
            counter += 1

    def visit_needed(self, entity: QAPISchemaEntity) -> bool:
        # Side effect: register all introspectable types now, so that
        # visit_end() can traverse them to discover type dependencies.
        if isinstance(entity, QAPISchemaType):
            if (not entity.is_implicit() or
                    isinstance(entity, QAPISchemaArrayType)):
                self._register_type(entity)
            return False
        return True

    def visit_command(self, name: str, info: Optional[QAPISourceInfo],
                      ifcond: QAPISchemaIfCond,
                      features: List[QAPISchemaFeature],
                      arg_type: Optional[QAPISchemaObjectType],
                      ret_type: Optional[QAPISchemaType], gen: bool,
                      success_response: bool, boxed: bool, allow_oob: bool,
                      allow_preconfig: bool, coroutine: bool) -> None:
        assert self._schema is not None
        self._register_type(arg_type or self._schema.the_empty_object_type)
        self._register_type(ret_type or self._schema.the_empty_object_type)

    def visit_event(self, name: str, info: Optional[QAPISourceInfo],
                    ifcond: QAPISchemaIfCond,
                    features: List[QAPISchemaFeature],
                    arg_type: Optional[QAPISchemaObjectType],
                    boxed: bool) -> None:
        assert self._schema is not None
        self._register_type(arg_type or self._schema.the_empty_object_type)

    def visit_object_type_flat(
            self, name: str, info: Optional[QAPISourceInfo],
            ifcond: QAPISchemaIfCond,
            features: List[QAPISchemaFeature],
            members: List[QAPISchemaObjectTypeMember],
            branches: Optional[QAPISchemaBranches]) -> None:
        for m in members:
            self._register_type(m.type)
        if branches:
            for v in branches.variants:
                self._register_type(v.type)

    def visit_alternate_type(
            self, name: str, info: Optional[QAPISourceInfo],
            ifcond: QAPISchemaIfCond,
            features: List[QAPISchemaFeature],
            alternatives: QAPISchemaAlternatives) -> None:
        for m in alternatives.variants:
            self._register_type(m.type)

    def _register_type(self, typ: QAPISchemaType) -> None:
        """Record a type for introspection (idempotent)."""
        typ = self._canonicalize_type(typ)
        if typ not in self._types_set:
            self._types.append(typ)
            self._types_set.add(typ)
            if isinstance(typ, QAPISchemaArrayType):
                self._register_type(typ.element_type)

    def _canonicalize_type(self, typ: QAPISchemaType) -> QAPISchemaType:
        """Canonicalize integer types to plain int."""
        assert self._schema is not None
        if typ.json_type() == 'int':
            type_int = self._schema.lookup_type('int')
            assert type_int
            return type_int
        if (isinstance(typ, QAPISchemaArrayType) and
                typ.element_type.json_type() == 'int'):
            type_intlist = self._schema.lookup_type('intList')
            assert type_intlist
            return type_intlist
        return typ

    def masked_name(self, name: str) -> str:
        """Return the masked name for a non-builtin, non-array type."""
        assert name in self._name_map, \
            f"type '{name}' was not registered or is builtin/array"
        return self._name_map[name]

    def introspection_name(self, typ: QAPISchemaType) -> str:
        """Return the introspection name for a type."""
        typ = self._canonicalize_type(typ)
        if isinstance(typ, QAPISchemaBuiltinType):
            return typ.name
        if isinstance(typ, QAPISchemaArrayType):
            return '[' + self.introspection_name(typ.element_type) + ']'
        assert typ in self._types_set
        return self.masked_name(typ.name)

    def types(self) -> Sequence[QAPISchemaType]:
        """Return the types to include in QAPI introspection."""
        return self._types
