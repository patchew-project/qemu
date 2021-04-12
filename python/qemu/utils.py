"""
QEMU utility library

This offers miscellaneous utility functions, which may not be easily
distinguishable or numerous to be in their own module.
"""

# Copyright (C) 2021 Red Hat Inc.
#
# Authors:
#  Cleber Rosa <crosa@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.  See
# the COPYING file in the top-level directory.
#

import re
from typing import Optional


def get_info_usernet_hostfwd_port(info_usernet_output: str) -> Optional[int]:
    """
    Returns the port given to the hostfwd parameter via info usernet

    :param info_usernet_output: output generated by hmp command "info usernet"
    :return: the port number allocated by the hostfwd option
    """
    for line in info_usernet_output.split('\r\n'):
        regex = r'TCP.HOST_FORWARD.*127\.0\.0\.1\s+(\d+)\s+10\.'
        match = re.search(regex, line)
        if match is not None:
            return int(match[1])
    return None
