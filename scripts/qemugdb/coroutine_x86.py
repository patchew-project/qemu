#!/usr/bin/python

# GDB debugging support
#
# Copyright 2019 Red Hat, Inc.
#
# Authors:
#  Paolo Bonzini <pbonzini@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import gdb

U64_PTR = gdb.lookup_type('uint64_t').pointer()

def get_coroutine_regs(addr):
    addr = addr.cast(gdb.lookup_type('CoroutineX86').pointer())
    rsp = addr['sp'].cast(U64_PTR)
    return {'rsp': rsp,
            'rip': rsp.dereference()}
