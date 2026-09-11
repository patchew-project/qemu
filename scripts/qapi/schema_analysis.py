# SPDX-License-Identifier: GPL-2.0-or-later
"""
Collect introspectable types from a QAPI schema and assign masked names.

Copyright (C) 2015-2026 Red Hat, Inc.

Authors:
 Markus Armbruster <armbru@redhat.com>
 John Snow <jsnow@redhat.com>
 Marc-André Lureau <marcandre.lureau@redhat.com>
"""

from typing import Dict, List, Sequence

from .schema import (
    QAPISchema,
    QAPISchemaArrayType,
    QAPISchemaBuiltinType,
    QAPISchemaEntity,
    QAPISchemaType,
    QAPISchemaVisitor,
)


class QAPISchemaTypeAnalysis(QAPISchemaVisitor):
    """Analyze types from a QAPI schema.

    Every included non-builtin, non-array type is assigned an introspection
    name. Names are masked as integer strings unless unmasking is requested.
    """

    def __init__(self, unmask: bool):
        self._unmask = unmask
        self._types: List[QAPISchemaType] = []
        self._name_map: Dict[str, str] = {}

    def visit_begin(self, schema: QAPISchema) -> None:
        self._types = []
        self._name_map = {}

    def visit_needed(self, entity: QAPISchemaEntity) -> bool:
        if isinstance(entity, QAPISchemaType) and entity.introspectable:
            self._types.append(entity)
            if not isinstance(entity, (QAPISchemaBuiltinType,
                                       QAPISchemaArrayType)):
                self._name_map[entity.name] = (
                    entity.name if self._unmask else str(len(self._name_map)))
        return False

    def masked_name(self, name: str) -> str:
        """Return a non-builtin, non-array type's assigned introspection name.

        Return the original name when unmasking is requested.
        """
        assert name in self._name_map, \
            f"type '{name}' has no assigned introspection name"
        return self._name_map[name]

    def introspection_name(self, typ: QAPISchemaType) -> str:
        """Return the introspection name for a type."""
        if isinstance(typ, QAPISchemaBuiltinType):
            return typ.name
        if isinstance(typ, QAPISchemaArrayType):
            return '[' + self.introspection_name(typ.element_type) + ']'
        return self.masked_name(typ.name)

    def types(self) -> Sequence[QAPISchemaType]:
        """Return the types to include in QAPI introspection."""
        return self._types
