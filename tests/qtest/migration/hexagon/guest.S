/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Migration checkpoint guest, loaded at 0x80010000 on sm8975-nsp.
 * r16 points to the host/guest mailbox at 0x80020000. The host releases
 * the checkpoint by writing 1 to mailbox[0]. No stack or runtime is used.
 */
#include "state.h"

.text
.globl _start
_start:
    r16 = ##STATE_MAILBOX
    r0 = htid
    p0 = cmp.eq(r0, #0)
    if (!p0) jump peer
    r0 = ##STATE_KERNEL_ADDR
    evb = r0

    /* Distinct state in two shared HVX contexts. */
    r0 = ssr
    r0 = setbit(r0, #31)
    ssr = r0
    r0 = #0x35
    v0.b = vsplat(r0)
    r0 = #0x57
    v31.b = vsplat(r0)
    q3 = vcmp.eq(v0.b, v0.b)
    r0 = ssr
    r0 = setbit(r0, #27)
    ssr = r0
    r0 = #0x46
    v0.b = vsplat(r0)
    r0 = #0x68
    v31.b = vsplat(r0)
    q3 = vcmp.eq(v0.b, v31.b)

    /*
     * Distinct patterns in the two shared HLX contexts. Raw encodings allow
     * regeneration with assemblers that do not support HLX mnemonics.
     */
    r0 = ##0x10000000
    ccr = r0
    r10 = ##0x1234
    .word 0xe52ae42c /* L6.h = r10.h */
    r0 = ##0x10200000
    ccr = r0
    r10 = ##0x5678
    .word 0xe52ae42c /* L6.h = r10.h */

    /* Keep both an owner and a blocked waiter across the checkpoint. */
    k0lock
    r0 = #2
    start(r0)
    r20 = ##0xdeadbeef
    r21 = ##0x76543210
    r0 = ##0xa55aff00
    p3:0 = r0
    r0 = ##0x31415926
    sgp0 = r0
    memw(r16 + #STATE_MEMORY) = r20
    memw(r16 + #STATE_READY) = #1
checkpoint:
    r1 = memw(r16 + #STATE_HEARTBEAT)
    r1 = add(r1, #1)
    memw(r16 + #STATE_HEARTBEAT) = r1
    r0 = memw(r16 + #STATE_RELEASE)
    p0 = cmp.eq(r0, #0)
    if (!p0) jump released
    pause(#1)
    jump checkpoint
released:
    memw(r16 + #STATE_R20) = r20
    memw(r16 + #STATE_R21) = r21
    r0 = p3:0
    memw(r16 + #STATE_PREDICATES) = r0
    r0 = ssr
    memw(r16 + #STATE_SSR) = r0
    r0 = ccr
    memw(r16 + #STATE_CCR) = r0
    r0 = sgp0
    memw(r16 + #STATE_SGP0) = r0

    /* Read the active contexts before changing the selection. */
    r0 = ##STATE_HVX1
    vmem(r0) = v0
    vmem(r0 + #1) = v31
    v1 = vmux(q3, v0, v31)
    vmem(r0 + #2) = v1
    r6 = ##STATE_HLX1
    /* The producer and .new store form one packet. */
    .word 0xe50079ac /* L6.h = L6.h */
    .word 0xa486c006 /* mem(r6) = L6.new */

    r0 = ssr
    r0 = clrbit(r0, #27)
    ssr = r0
    r0 = ##STATE_HVX0
    vmem(r0) = v0
    vmem(r0 + #1) = v31
    v1 = vmux(q3, v0, v31)
    vmem(r0 + #2) = v1
    r0 = ##0x10000000
    ccr = r0
    r6 = ##STATE_HLX0
    /* The producer and .new store form one packet. */
    .word 0xe50079ac /* L6.h = L6.h */
    .word 0xa486c006 /* mem(r6) = L6.new */

    k0unlock
    memw(r16 + #STATE_DONE) = #1
done:
    pause(#1)
    jump done

peer:
    r22 = ##0xcafef00d
    memw(r16 + #STATE_PEER_READY) = #1
.globl peer_lock
peer_lock:
    k0lock
    memw(r16 + #STATE_PEER_R22) = r22
    k0unlock
    memw(r16 + #STATE_PEER_DONE) = #1
    jump done
