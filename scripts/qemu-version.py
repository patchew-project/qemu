#!/usr/bin/env python3

#
# Script for retrieve qemu git version information
#
# Author: Yonggang Luo <luoyonggang@gmail.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.  See
# the COPYING file in the top-level directory.
#

import sys
import subprocess

def main(args):
    pkgversion = ''
    version = ''
    if len(args) > 3:
        dir = args[1]
        pkgversion = args[2]
        version = args[3]
        if len(pkgversion) == 0:
            pc = subprocess.run(['git', 'describe', '--match', "'v*'", '--dirty', '--always'],
                                stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, cwd=dir)
            if pc.returncode == 0:
                pkgversion = pc.stdout.decode('utf8').strip()

    fullversion = version
    if len(pkgversion) > 0:
        fullversion = "{} ({})".format(version, pkgversion)

    version_header = '''#define QEMU_PKGVERSION "{}"
#define QEMU_FULL_VERSION "{}"'''.format(pkgversion, fullversion)
    sys.stdout.buffer.write(version_header.encode('utf8'))

if __name__ == "__main__":
    main(sys.argv)
