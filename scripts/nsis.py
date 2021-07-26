#!/usr/bin/env python3
#
# Copyright (C) 2020 Red Hat, Inc.
#
# SPDX-License-Identifier: GPL-2.0-or-later

import argparse
import glob
import os
import sys
import shutil
import subprocess
import tempfile


def signcode(path):
    cmd = os.environ.get("SIGNCODE")
    if not cmd:
        return
    subprocess.run([cmd, path])


def copydlls(binary, srcdir, dstdir):
    cmdline = [ "objdump", "-p", binary ]
    result = subprocess.run(cmdline, stdout = subprocess.PIPE,
                            universal_newlines = True)
    if result.returncode != 0:
        sys.exit(result.returncode)
    for line in result.stdout.split('\n'):
        if line.find('DLL Name') != -1:
            dll = line.split()[2]
            src = os.path.join(srcdir, dll)
            dst = os.path.join(dstdir, dll)
            if os.path.isfile(src) and not os.path.isfile(dst):
                print("nsis.py: copy " + src)
                shutil.copyfile(src, dst)
                copydlls(src, srcdir, dstdir)


def main():
    parser = argparse.ArgumentParser(description="QEMU NSIS build helper.")
    parser.add_argument("outfile")
    parser.add_argument("prefix")
    parser.add_argument("srcdir")
    parser.add_argument("cpu")
    parser.add_argument("dllsrc")
    parser.add_argument("nsisargs", nargs="*")
    args = parser.parse_args()

    destdir = tempfile.mkdtemp()
    dlldir = tempfile.mkdtemp()
    try:
        subprocess.run(["make", "install", "DESTDIR=" + destdir + os.path.sep])
        with open(
            os.path.join(destdir + args.prefix, "system-emulations.nsh"), "w"
        ) as nsh:
            for exe in glob.glob(
                os.path.join(destdir + args.prefix, "qemu-system-*.exe")
            ):
                exe = os.path.basename(exe)
                arch = exe[12:-4]
                nsh.write(
                    """
                Section "{0}" Section_{0}
                SetOutPath "$INSTDIR"
                File "${{BINDIR}}\\{1}"
                SectionEnd
                """.format(
                        arch, exe
                    )
                )

        for exe in glob.glob(os.path.join(destdir + args.prefix, "*.exe")):
            signcode(exe)
            copydlls(exe, args.dllsrc, dlldir)

        makensis = [
            "makensis",
            "-V2",
            "-NOCD",
            "-DSRCDIR=" + args.srcdir,
            "-DBINDIR=" + destdir + args.prefix,
            "-DDLLDIR=" + dlldir,
        ]
        if args.cpu == "x86_64":
            makensis += ["-DW64"]

        makensis += ["-DOUTFILE=" + args.outfile] + args.nsisargs
        subprocess.run(makensis)
        signcode(args.outfile)
    finally:
        shutil.rmtree(destdir)
        shutil.rmtree(dlldir)


if __name__ == "__main__":
    main()
