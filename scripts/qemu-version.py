#!/usr/bin/env python3

#
# Script for retrieve qemu git version information
#
# Authors:
#  Yonggang Luo <luoyonggang@gmail.com>
#
# This work is licensed under the terms of the GNU GPL, version 2
# or, at your option, any later version.  See the COPYING file in
# the top-level directory.

import sys
import subprocess
import os, os.path
import platform

def main(_program, dir, pkgversion, version, *unused):
    os.chdir(dir)
    if not pkgversion and os.path.exists('.git'):
        match_expression = '--match=v\\*' if platform.system() == 'Windows' else '--match=v*'
        pc = subprocess.run(['git', 'describe', match_expression, '--always'],
                            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, encoding='utf8', shell=False)
        if pc.returncode == 0:
            pkgversion = pc.stdout.strip()

    fullversion = version
    if pkgversion:
        fullversion = "{} ({})".format(version, pkgversion)

    print('#define QEMU_PKGVERSION "%s"' % pkgversion)
    print('#define QEMU_FULL_VERSION "%s"' % fullversion)

if __name__ == "__main__":
    main(*sys.argv)
