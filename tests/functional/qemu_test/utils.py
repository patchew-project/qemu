# Utilities for python-based QEMU tests
#
# Copyright 2024 Red Hat, Inc.
#
# Authors:
#  Thomas Huth <thuth@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import lzma
import shutil
import tarfile

def archive_extract(archive, dest_dir, member=None):
    with tarfile.open(archive) as tf:
        if hasattr(tarfile, 'data_filter'):
            tf.extraction_filter = getattr(tarfile, 'data_filter',
                                           (lambda member, path: member))
        if member:
            tf.extract(member=member, path=dest_dir)
        else:
            tf.extractall(path=dest_dir)

def lzma_uncompress(xz_path, output_path):
    with lzma.open(xz_path, 'rb') as lzma_in:
        with open(output_path, 'wb') as raw_out:
            shutil.copyfileobj(lzma_in, raw_out)
