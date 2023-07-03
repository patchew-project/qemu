#!/usr/bin/env python3

import re
import sys

file_path = sys.argv[1]

with open(file_path, 'r') as f:
    for line in f.readlines():
        match = re.search('\"(/migration/.*)\"', line)
        if match:
            print(match.groups()[0])
