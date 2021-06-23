#!/usr/bin/python3
#
# Re-generate container recipes
#
# This script uses the "lcitool" available from
#
#   https://gitlab.com/libvirt/libvirt-ci
#
# Copyright (c) 2020 Red Hat Inc.
#
# This work is licensed under the terms of the GNU GPL, version 2
# or (at your option) any later version. See the COPYING file in
# the top-level directory.

import sys
import os
import os.path
import subprocess

if len(sys.argv) != 3:
   print("syntax: %s PATH-TO-LCITOOL SRC-ROOT" % sys.argv[0], file=sys.stderr)
   sys.exit(1)

lcitool_path=sys.argv[1]
src_root=sys.argv[2]

def atomic_write(filename, content):
   dst = os.path.join(src_root, "tests", "docker", "dockerfiles", filename)
   try:
      with open(dst + ".tmp", "w") as fp:
         print(content, file=fp, end="")
         os.replace(dst + ".tmp", dst)
   except Exception as ex:
      os.unlink(dst + ".tmp")
      raise

def generate_image(filename, host, cross=None, trailer=None):
   print("Generate %s" % filename)
   args = [lcitool_path, "dockerfile"]
   if cross is not None:
      args.extend(["--cross", cross])
   args.extend([host, "qemu"])
   lcitool=subprocess.run(args, capture_output=True)

   if lcitool.returncode != 0:
      raise Exception("Failed to generate %s: %s" % (filename, lcitool.stderr))

   content = lcitool.stdout.decode("utf8")
   if trailer is not None:
      content += trailer
   atomic_write(filename, content)

try:
   generate_image("centos8.docker", "centos-8")
   generate_image("fedora.docker", "fedora-33")
except Exception as ex:
   print(str(ex), file=sys.stderr)
