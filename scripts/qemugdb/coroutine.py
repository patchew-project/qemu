#!/usr/bin/python

# GDB debugging support, coroutine dispatch
#
# Copyright 2012 Red Hat, Inc. and/or its affiliates
#
# Authors:
#  Avi Kivity <avi@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2
# or later.  See the COPYING file in the top-level directory.

from . import coroutine_ucontext
import gdb

VOID_PTR = gdb.lookup_type('void').pointer()
UINTPTR_T = gdb.lookup_type('uintptr_t')

backends = {
    'CoroutineUContext': coroutine_ucontext
}

def coroutine_backend():
    for k, v in backends.items():
        try:
            gdb.lookup_type(k)
        except:
            continue
        return v

    raise Exception('could not find coroutine backend')

class CoroutineCommand(gdb.Command):
    '''Display coroutine backtrace'''
    def __init__(self):
        gdb.Command.__init__(self, 'qemu coroutine', gdb.COMMAND_DATA,
                             gdb.COMPLETE_NONE)

    def invoke(self, arg, from_tty):
        argv = gdb.string_to_argv(arg)
        if len(argv) != 1:
            gdb.write('usage: qemu coroutine <coroutine-pointer>\n')
            return

        addr = gdb.parse_and_eval(argv[0])
        regs = coroutine_backend().get_coroutine_regs(addr)
        old = dict()

        for i in regs:
            old[i] = gdb.parse_and_eval('(uint64_t)$%s' % i)

        for i in regs:
            gdb.execute('set $%s = %s' % (i, regs[i].cast(UINTPTR_T)))

        gdb.execute('bt')

        for i in regs:
            gdb.execute('set $%s = %s' % (i, old[i].cast(UINTPTR_T)))

class CoroutineSPFunction(gdb.Function):
    def __init__(self):
        gdb.Function.__init__(self, 'qemu_coroutine_sp')

    def invoke(self, addr):
        return coroutine_backend().get_coroutine_regs(addr)['rsp'].cast(VOID_PTR)

class CoroutinePCFunction(gdb.Function):
    def __init__(self):
        gdb.Function.__init__(self, 'qemu_coroutine_pc')

    def invoke(self, addr):
        return coroutine_backend().get_coroutine_regs(addr)['rip'].cast(VOID_PTR)
