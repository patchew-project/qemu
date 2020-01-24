#!/usr/bin/env python
#
# VM testing aarch64 library
#
# Copyright 2020 Linaro
#
# Authors:
#  Robert Foley <robert.foley@linaro.org>
#
# This code is licensed under the GPL version 2 or later.  See
# the COPYING file in the top-level directory.
#
import os
import sys
import subprocess
import basevm


def create_flash_images():
    """Creates the appropriate pflash files
       for an aarch64 VM."""
    subprocess.check_call(["dd", "if=/dev/zero", "of=flash0.img",
                           "bs=1M", "count=64"])
    # A reliable way to get the QEMU EFI image is via an installed package.
    efi_img = "/usr/share/qemu-efi-aarch64/QEMU_EFI.fd"
    if not os.path.exists(efi_img):
        sys.stderr.write("*** {} is missing\n".format(efi_img))
        sys.stderr.write("*** please install qemu-efi-aarch64 package\n")
        exit(3)
    subprocess.check_call(["dd", "if={}".format(efi_img),
                           "of=flash0.img", "conv=notrunc"])
    subprocess.check_call(["dd", "if=/dev/zero",
                           "of=flash1.img", "bs=1M", "count=64"])

def get_pflash_args():
    """Returns a string that can be used to
       boot qemu using the appropriate pflash files
       for aarch64."""
    pflash_args = "-drive file=flash0.img,format=raw,if=pflash "\
                  "-drive file=flash1.img,format=raw,if=pflash"
    return pflash_args.split(" ")
