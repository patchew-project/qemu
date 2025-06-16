# SPDX-License-Identifier: GPL-2.0-or-later

import os
import re
import sys

if not os.path.exists("qapi-schema.json"):
    raise Exception(
        "This script was meant to be run from the qemu.git/qapi directory."
    )
sys.path.append("../scripts/")

from qapi.schema import QAPISchema, QAPISchemaDefinition

# Adjust this global to exclude certain tokens from being xreffed.
SKIP_TOKENS = ('String', 'stop', 'transaction', 'eject', 'migrate', 'quit')

print("Compiling schema to build list of reference-able entities ...", end='')
tokens = []
schema = QAPISchema("qapi-schema.json")
for ent in schema._entity_list:
    if isinstance(ent, QAPISchemaDefinition) and not ent.is_implicit():
        if ent.name not in SKIP_TOKENS:
            tokens.append(ent.name)
print("OK")

patt_names = r'(' + '|'.join(tokens) + r')'

# catch 'token' and "token" specifically
patt = re.compile(r'([\'"]|``)' + patt_names + r'\1')
# catch naked instances of token, excluding those where prefixed or
# suffixed by a quote, dash, or word character. Exclude "@" references
# specifically to handle them elsewhere. Exclude <name> matches, as
# these are explicit cross-reference targets.
patt2 = r"(?<![-@`'\"\w<])" + patt_names + r"(?![-`'\"\w>])"
# catch @references. prohibit when followed by ":" to exclude members
# whose names happen to match xreffable entities.
patt3 = r"@" + patt_names + r"(?![-\w:])"




for file in os.scandir():
    outlines = []
    if not file.name.endswith(".json"):
        continue
    print(f"Scanning {file.name} ...")
    with open(file.name) as searchfile:
        block_start = False
        for line in searchfile:
            # Don't mess with the start of doc blocks.
            # We don't want to convert "# @name:" to a reference!
            if block_start and line.startswith('# @'):
                outlines.append(line)
                continue
            block_start = bool(line.startswith('##'))

            # Don't mess with anything outside of comment blocks,
            # and don't mess with example blocks. We use five spaces
            # as a heuristic for detecting example blocks. It's not perfect,
            # but it seemingly does the job well.
            if line.startswith('# ') and not line.startswith('#     '):
                line = re.sub(patt, r'`\2`', line)
                line = re.sub(patt2, r'`\1`', line)
                line = re.sub(patt3, r'`\1`', line)
            outlines.append(line)
    with open(file.name, "w") as outfile:
        for line in outlines:
            outfile.write(line)
