"""
QEMU feature module:

This module provides a utility for discovering the availability of
generic features.
"""
# Copyright (C) 2022 Red Hat Inc.
#
# Authors:
#  Cleber Rosa <crosa@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.  See
# the COPYING file in the top-level directory.
#

import logging
import subprocess
from typing import List


LOG = logging.getLogger(__name__)


def list_feature(qemu_bin: str, feature: str) -> List[str]:
    """
    List available options the QEMU binary for a given feature type.

    By calling a "qemu $feature -help" and parsing its output.

    @param qemu_bin (str): path to the QEMU binary.
    @param feature (str): feature name, matching the command line option.
    @raise Exception: if failed to run `qemu -feature help`
    @return a list of available options for the given feature.
    """
    if not qemu_bin:
        return []
    try:
        out = subprocess.check_output([qemu_bin, '-%s' % feature, 'help'],
                                      universal_newlines=True)
    except:
        LOG.debug("Failed to get the list of %s(s) in %s", feature, qemu_bin)
        raise
    # Skip the first line which is the header.
    return [item.split(' ', 1)[0] for item in out.splitlines()[1:]]
