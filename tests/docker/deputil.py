#!/usr/bin/env python3
import os
import re
import sys
from typing import IO, Optional

def get_dep(infile: IO[str]) -> Optional[str]:
    """Get a dependency as a string from a dockerfile."""
    for line in infile:
        match = re.match(r'FROM (.+)', line)
        if match:
            return match[1]
    return None

def get_qemu_dep(infile: IO[str]) -> Optional[str]:
    """Get a dependency on the qemu: namespace from a dockerfile."""
    dep = get_dep(infile) or ''
    match = re.match(r'qemu:(.+)', dep)
    return match[1] if match else None

def main() -> None:
    filename = sys.argv[1]
    basefile = os.path.basename(filename)
    base = os.path.splitext(basefile)[0]
    depfile = f"{base}.d"
    deps = [filename]

    print(f"{depfile}: {filename}")
    with open(filename, "r") as infile:
        match = get_qemu_dep(infile) or ''
        if match:
            deps.append(f"docker-image-{match}")
    print("{}: {}".format(
        f"docker-image-{base}",
        " ".join(deps)
    ))

if __name__ == '__main__':
    main()
