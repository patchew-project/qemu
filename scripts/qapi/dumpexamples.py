"""
Dump examples for Developers
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
import json
import random
import string

from typing import Dict, List, Optional

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


def gen_examples(schema: QAPISchema,
                 output_dir: str,
                 prefix: str) -> None:
    vis = QAPISchemaGenExamplesVisitor(prefix)
    schema.visit(vis)
    vis.write(output_dir)


def get_id(random, size: int) -> str:
    letters = string.ascii_lowercase
    return ''.join(random.choice(letters) for i in range(size))


def next_object(text, start, end, context) -> Dict:
    # Start of json object
    start = text.find("{", start)
    end = text.rfind("}", start, end+1)

    # try catch, pretty print issues
    try:
        ret = json.loads(text[start:end+1])
    except Exception as e:
        print("Error: {}\nLocation: {}\nData: {}\n".format(
              str(e), context, text[start:end+1]))
        return {}
    else:
        return ret


def parse_text_to_dicts(text: str, context: str) -> List[Dict]:
    examples, clients, servers = [], [], []

    count = 1
    c, s = text.find("->"), text.find("<-")
    while c != -1 or s != -1:
        if c == -1 or (s != -1 and s < c):
            start, target = s, servers
        else:
            start, target = c, clients

        # Find the client and server, if any
        if c != -1:
            c = text.find("->", start + 1)
        if s != -1:
            s = text.find("<-", start + 1)

        # Find the limit of current's object.
        # We first look for the next message, either client or server. If none
        # is avaible, we set the end of the text as limit.
        if c == -1 and s != -1:
            end = s
        elif c != -1 and s == -1:
            end = c
        elif c != -1 and s != -1:
            end = (c < s) and c or s
        else:
            end = len(text) - 1

        message = next_object(text, start, end, context)
        if len(message) > 0:
            message_type = "return"
            if "execute" in message:
                message_type = "command"
            elif "event" in message:
                message_type = "event"

            target.append({
                "sequence-order": count,
                "message-type": message_type,
                "message": message
            })
            count += 1

    examples.append({"client": clients, "server": servers})
    return examples


def parse_examples_of(self: QAPISchemaGenExamplesVisitor,
                      name: str):

    assert(name in self.schema._entity_dict)
    obj = self.schema._entity_dict[name]
    assert((obj.doc is not None))
    module_name = obj._module.name

    # We initialize random with the name so that we get consistent example
    # ids over different generations. The ids of a given example might
    # change when adding/removing examples, but that's acceptable as the
    # goal is just to grep $id to find what example failed at a given test
    # with minimum chorn over regenerating.
    random.seed(name, version=2)

    for s in obj.doc.sections:
        if s.name != "Example":
            continue

        if module_name not in self.target:
            self.target[module_name] = []

        context = f'''{name} at {obj.info.fname}:{obj.info.line}'''
        examples = parse_text_to_dicts(s.text, context)
        for example in examples:
            self.target[module_name].append({
                    "id": get_id(random, 10),
                    "client": example["client"],
                    "server": example["server"]
            })


class QAPISchemaGenExamplesVisitor(QAPISchemaVisitor):

    def __init__(self, prefix: str):
        super().__init__()
        self.target = {}
        self.schema = None

    def visit_begin(self, schema):
        self.schema = schema

    def visit_end(self):
        self.schema = None

    def write(self: QAPISchemaGenExamplesVisitor,
              output_dir: str) -> None:
        for filename, content in self.target.items():
            pathname = os.path.join(output_dir, "examples", filename)
            odir = os.path.dirname(pathname)
            os.makedirs(odir, exist_ok=True)
            result = {"examples": content}

            with open(pathname, "w") as outfile:
                outfile.write(json.dumps(result, indent=2, sort_keys=True))

    def visit_command(self: QAPISchemaGenExamplesVisitor,
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

        if gen:
            parse_examples_of(self, name)

    def visit_event(self: QAPISchemaGenExamplesVisitor,
                    name: str,
                    info: Optional[QAPISourceInfo],
                    ifcond: QAPISchemaIfCond,
                    features: List[QAPISchemaFeature],
                    arg_type: Optional[QAPISchemaObjectType],
                    boxed: bool):

        parse_examples_of(self, name)
