#!/usr/bin/env python3
"""
QAPI code generation execution shim.

This file exists primarily to facilitate the running of the QAPI code
generator without needing to install the python module to the current
execution environment.
"""

import sys

from qapi import script

if __name__ == '__main__':
    sys.exit(script.main())
