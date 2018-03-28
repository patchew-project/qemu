# Coredump patching
#
# Copyright (c) 2018 Virtuozzo International GmbH. All rights reserved.
#
# Authors:
#  Vladimir Sementsov-Ogievskiy <vsementsov@virtuozzo.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

import struct
import shutil

def write_regs_to_coredump(fname, set_regs):
    # asm/ptrace.h
    pt_regs = ['r15', 'r14', 'r13', 'r12', 'rbp', 'rbx', 'r11', 'r10',
               'r9', 'r8', 'rax', 'rcx', 'rdx', 'rsi', 'rdi', 'orig_rax',
               'rip', 'cs', 'eflags', 'rsp', 'ss']

    with open(fname, 'r+b') as f:
        print 'patching core file "%s"' % fname

        while f.read(4) != 'CORE':
            pass

        print 'found "CORE" at 0x%x' % f.tell()
        f.seek(4, 1) # go to elf_prstatus
        f.seek(112, 1) # offsetof(struct elf_prstatus, pr_reg)

        print 'assume pt_regs at 0x%x' % f.tell()
        for reg in pt_regs:
            if reg in set_regs:
                print 'write %s at 0x%x' % (reg, f.tell())
                f.write(struct.pack('q', set_regs[reg]))
            else:
                f.seek(8, 1)

def clone_coredump(source, target, set_regs):
    shutil.copyfile(source, target)
    write_regs_to_coredump(target, set_regs)
