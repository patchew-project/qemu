#!/usr/bin/env python3

import simplebench
from qemu.bench_block_job import bench_block_copy, drv_file, drv_nbd


def bench_func(env, case):
    return bench_block_copy(env['qemu_binary'], env['cmd'],
                            case['source'], case['target'])


test_cases = [
    {
        'id': 'ssd -> ssd',
        'source': drv_file('/ssd/ones1000M-source'),
        'target': drv_file('/ssd/ones1000M-target')
    },
    {
        'id': 'ssd -> hdd',
        'source': drv_file('/ssd/ones1000M-source'),
        'target': drv_file('/test-a/ones1000M-target')
    },
    {
        'id': 'hdd -> hdd',
        'source': drv_file('/test-a/ones1000M-source'),
        'target': drv_file('/test-a/ones1000M-target')
    }
]

test_envs = [
    {
        'id': 'backup-old',
        'cmd': 'blockdev-backup',
        'qemu_binary': '/work/src/qemu/up-block-copy-block-status--before/x86_64-softmmu/qemu-system-x86_64'
    },
    {
        'id': 'backup-old(no CR)',
        'cmd': 'blockdev-backup',
        'qemu_binary': '/work/src/qemu/up-block-copy-block-status--before--no-copy-range/x86_64-softmmu/qemu-system-x86_64'
    },
    {
        'id': 'backup-new',
        'cmd': 'blockdev-backup',
        'qemu_binary': '/work/src/qemu/up-block-copy-block-status/x86_64-softmmu/qemu-system-x86_64'
    },
    {
        'id': 'backup-new(no CR)',
        'cmd': 'blockdev-backup',
        'qemu_binary': '/work/src/qemu/up-block-copy-block-status--no-copy_range/x86_64-softmmu/qemu-system-x86_64'
    },
    {
        'id': 'mirror',
        'cmd': 'blockdev-mirror',
        'qemu_binary': '/work/src/qemu/up-block-copy-block-status/x86_64-softmmu/qemu-system-x86_64'
    }
]

result = simplebench.bench(bench_func, test_envs, test_cases, count=3)
print(simplebench.ascii(result))

test_cases = [
    {
        'id': 'nbd -> ssd',
        'source': drv_nbd('172.16.24.200', '10810'),
        'target': drv_file('/ssd/ones1000M-target')
    },
    {
        'id': 'ssd -> nbd',
        'source': drv_file('/ssd/ones1000M-target'),
        'target': drv_nbd('172.16.24.200', '10809')
    },
]

test_envs = [
    {
        'id': 'backup-old',
        'cmd': 'blockdev-backup',
        'qemu_binary': '/work/src/qemu/up-block-copy-block-status--before/x86_64-softmmu/qemu-system-x86_64'
    },
    {
        'id': 'backup-new',
        'cmd': 'blockdev-backup',
        'qemu_binary': '/work/src/qemu/up-block-copy-block-status/x86_64-softmmu/qemu-system-x86_64'
    },
    {
        'id': 'mirror',
        'cmd': 'blockdev-mirror',
        'qemu_binary': '/work/src/qemu/up-block-copy-block-status/x86_64-softmmu/qemu-system-x86_64'
    }
]

result = simplebench.bench(bench_func, test_envs, test_cases, count=2)
print(simplebench.ascii(result))
