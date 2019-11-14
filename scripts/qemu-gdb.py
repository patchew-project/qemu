#!/usr/bin/python

# GDB debugging support
#
# Copyright 2012 Red Hat, Inc. and/or its affiliates
#
# Authors:
#  Avi Kivity <avi@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

# Usage:
# At the (gdb) prompt, type "source scripts/qemu-gdb.py".
# "help qemu" should then list the supported QEMU debug support commands.

import gdb

import os, sys

if sys.version_info[0] < 3:
    int = long

# Annoyingly, gdb doesn't put the directory of scripts onto the
# module search path. Do it manually.

sys.path.append(os.path.dirname(__file__))

from qemugdb import aio, mtree, coroutine, tcg, timers

class QemuCommand(gdb.Command):
    '''Prefix for QEMU debug support commands'''
    def __init__(self):
        gdb.Command.__init__(self, 'qemu', gdb.COMMAND_DATA,
                             gdb.COMPLETE_NONE, True)

QemuCommand()
coroutine.CoroutineCommand()
mtree.MtreeCommand()
aio.HandlersCommand()
tcg.TCGLockStatusCommand()
timers.TimersCommand()

coroutine.CoroutineSPFunction()
coroutine.CoroutinePCFunction()

# Default to silently passing through SIGUSR1, because QEMU sends it
# to itself a lot.
gdb.execute('handle SIGUSR1 pass noprint nostop')


def is_object(val):
    def is_object_helper(type):
        if str(type) == "Object":
            return True

        while type.code == gdb.TYPE_CODE_TYPEDEF:
            type = type.target()

        if type.code != gdb.TYPE_CODE_STRUCT:
            return False

        fields = type.fields()
        if len (fields) < 1:
            return False

        first_field = fields[0]
        return is_object_helper(first_field.type)

    type = val.type
    if type.code != gdb.TYPE_CODE_PTR:
        return False
    type = type.target()
    return is_object_helper (type)


def object_class_name(instance):
    try:
        inst = instance.cast(gdb.lookup_type("Object").pointer())
        klass = inst["class"]
        typ = klass["type"]
        return typ["name"].string()
    except RuntimeError:
        pass


class ObjectPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        name = object_class_name(self.val)
        if name:
            return ("0x%x [%s]")% (int(self.val), name)
        return  ("0x%x") % (int(self.val))


def lookup_type(val):
    if is_object(val):
        return ObjectPrinter(val)
    return None


gdb.pretty_printers.append(lookup_type)
