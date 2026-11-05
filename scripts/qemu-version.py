#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

# Usage: scripts/qemu-version.py -version PKGVERSION INPUT
# @VCS_TAG@ in INPUT is replaced with PKGVERSION. An empty PKGVERSION
# drops that line.

import argparse
import sys

parser = argparse.ArgumentParser()
parser.add_argument('-version', default='')
parser.add_argument('input')
args = parser.parse_args()

version = args.version
with open(args.input, 'rb') as template_file:
    template = template_file.read()
if version:
    version = version.replace('\\', '\\\\').replace('"', '\\"')
    template = template.replace(b'@VCS_TAG@', version.encode('utf-8'))
    sys.stdout.buffer.write(template)
else:
    kept = [line for line in template.splitlines(True)
            if b'@VCS_TAG@' not in line]
    sys.stdout.buffer.write(b''.join(kept) if kept else b'\n')
