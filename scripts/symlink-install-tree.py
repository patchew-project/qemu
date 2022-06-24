#!/usr/bin/env python3

from pathlib import Path
import errno
import json
import os
import subprocess
import sys

def destdir_join(d1: str, d2: str) -> str:
    if not d1:
        return d2
    d2_path = Path(d2)
    d2_parts = d2_path.parts
    # c:\destdir + c:\prefix must produce c:\destdir\prefix
    if d2_path.drive or d2_path.root:
        d2_parts = d2_parts[1:]
    return str(Path(d1, *d2_parts))

introspect = os.environ.get('MESONINTROSPECT')
out = subprocess.run([*introspect.split(' '), '--installed'],
                     stdout=subprocess.PIPE, check=True).stdout
for source, dest in json.loads(out).items():
    assert os.path.isabs(source)
    bundle_dest = destdir_join('qemu-bundle', dest)
    path = os.path.dirname(bundle_dest)
    try:
        os.makedirs(path, exist_ok=True)
    except BaseException as e:
        print(f'error making directory {path}', file=sys.stderr)
        raise e
    try:
        os.symlink(source, bundle_dest)
    except BaseException as e:
        if not isinstance(e, OSError) or e.errno != errno.EEXIST:
            print(f'error making symbolic link {dest}', file=sys.stderr)
            raise e
