#!/usr/bin/env python3

import glob
import os
import sys

from avocado.core.job import Job
from avocado.core.suite import TestSuite


def filter_out_currently_broken(variants):
    """Filter out targets that can not be currently used transparently."""
    result = []
    for variant in variants:
        if (# qemu-system-m68k: Kernel image must be specified
            variant['qemu_bin'].endswith('qemu-system-m68k') or
            # qemu-system-sh4: Could not load SHIX bios 'shix_bios.bin'
            variant['qemu_bin'].endswith('qemu-system-sh4')):
            continue
        result.append(variant)
    return result


def add_machine_type(variants):
    """Add default machine type  parameters to targets that require one."""
    for variant in variants:
        if (variant['qemu_bin'].endswith('-arm') or
            variant['qemu_bin'].endswith('-aarch64')):
            variant['machine'] = 'virt'
        if variant['qemu_bin'].endswith('-rx'):
            variant['machine'] = 'none'
        if variant['qemu_bin'].endswith('-avr'):
            variant['machine'] = 'none'


def all_system_binaries():
    """Looks for all system binaries and creates dict variants."""
    binaries = [target for target in glob.glob('./qemu-system-*')
                if (os.path.isfile(target) and
                    os.access(target, os.R_OK | os.X_OK))]
    variants = []
    for target in binaries:
        variants.append({'qemu_bin': target})
    variants = filter_out_currently_broken(variants)
    add_machine_type(variants)
    return variants


def main():
    suite1 = TestSuite.from_config(
        {'run.references': ['tests/acceptance/'],
         'filter.by_tags.tags': ['-arch'],
         'run.dict_variants': all_system_binaries()},
        name='non-arch-specific')

    suite2 = TestSuite.from_config(
        {'run.references': ['tests/acceptance/'],
         'filter.by_tags.tags': ['arch']},
        name='arch-specific')

    with Job({'job.run.result.html.enabled': 'on'},
             [suite1, suite2]) as job:
        return job.run()


if __name__ == '__main__':
    sys.exit(main())
