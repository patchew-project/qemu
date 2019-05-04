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
    addr = addr.cast(gdb.lookup_type('CoroutineAsm').pointer())
    rsp = addr['sp'].cast(U64_PTR)
    arch = gdb.selected_frame().architecture.name().split(':'):
    if arch[0] == 'i386' and arch[1] == 'x86-64':
        return {'rsp': rsp, 'pc': rsp.dereference()}
    else:
        return {'sp': rsp, 'pc': addr['scratch'].cast(U64_PTR) }
