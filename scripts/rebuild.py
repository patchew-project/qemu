#! /usr/bin/env python3
#
# Author: Paolo Bonzini <pbonzini@redhat.com>
#
# This program compiles the input files using commands from the
# compile_commands.json file.  (Unlike Make/ninja, the _source_
# file is passed to the program rather than the targe).  It is
# mostly intended to be called from editors.

import os
import sys
import json

with open('compile_commands.json') as f:
    cc_json = json.load(f)

paths = set((os.path.relpath(i) for i in sys.argv[1:]))
for i in cc_json:
    if i['file'] in paths:
        os.chdir(i['directory'])
        print(i['command'])
        os.system(i['command'])
