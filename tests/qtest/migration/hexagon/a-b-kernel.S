/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Dirty one byte per page in order, reporting progress on the virt UART. */
#include "../bootfile.h"

.text
.globl _start
_start:
    r0 = ##HEXAGON_TEST_MEM_START
    r1 = ##HEXAGON_TEST_MEM_END
    r2 = ##HEXAGON_MACH_VIRT_UART
    r3 = #0
    r4 = r0
clean:
    memb(r4) = r3
    r4 = add(r4, ##TEST_MEM_PAGE_SIZE)
    p0 = cmp.eq(r4, r1)
    if (!p0) jump clean
    r3 = #'A'
    memb(r2) = r3
    r5 = #0
mainloop:
    r4 = r0
innerloop:
    r3 = memb(r4)
    r3 = add(r3, #1)
    memb(r4) = r3
    r4 = add(r4, ##TEST_MEM_PAGE_SIZE)
    p0 = cmp.eq(r4, r1)
    if (!p0) jump innerloop
    r5 = add(r5, #1)
    r5 = and(r5, #31)
    p0 = cmp.eq(r5, #0)
    if (!p0) jump mainloop
    r3 = #'B'
    memb(r2) = r3
    pause(#1)
    jump mainloop
