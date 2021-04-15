#!/usr/bin/env python3

import os
import sys

# This comes from tests/acceptance/avocado_qemu/__init__.py and should
# not be duplicated here.  The solution is to have the "avocado_qemu"
# code and "python/qemu" available during build
BUILD_DIR = os.path.dirname(os.path.dirname(os.path.dirname(__file__)))
if os.path.islink(os.path.dirname(os.path.dirname(__file__))):
    # The link to the acceptance tests dir in the source code directory
    lnk = os.path.dirname(os.path.dirname(__file__))
    #: The QEMU root source directory
    SOURCE_DIR = os.path.dirname(os.path.dirname(os.readlink(lnk)))
else:
    SOURCE_DIR = BUILD_DIR
sys.path.append(os.path.join(SOURCE_DIR, 'python'))

from avocado.core.job import Job

from qemu.accel import kvm_available


def main():
    if not kvm_available():
        sys.exit(0)

    config = {'run.references': ['tests/acceptance/'],
              'filter.by_tags.tags': ['accel:kvm,arch:%s' % os.uname()[4]]}
    with Job.from_config(config) as job:
        return job.run()


if __name__ == '__main__':
    sys.exit(main())
