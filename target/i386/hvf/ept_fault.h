/*-
 * Copyright (c) 2011 NetApp, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef EPT_FAULT_H
#define EPT_FAULT_H

#include "hvf-i386.h"

static inline bool ept_emulation_fault(hvf_slot *slot, uint64_t gpa, uint64_t ept_qual)
{
    int read, write;

    /* EPT fault on an instruction fetch doesn't make sense here */
    if (ept_qual & EPT_VIOLATION_INST_FETCH) {
        return false;
    }

    /* EPT fault must be a read fault or a write fault */
    read = ept_qual & EPT_VIOLATION_DATA_READ ? 1 : 0;
    write = ept_qual & EPT_VIOLATION_DATA_WRITE ? 1 : 0;
    if ((read | write) == 0) {
        return false;
    }

    if (write && slot) {
        if (slot->flags & HVF_SLOT_LOG) {
            memory_region_set_dirty(slot->region, gpa - slot->start, 1);
            hv_vm_protect((hv_gpaddr_t)slot->start, (size_t)slot->size,
                          HV_MEMORY_READ | HV_MEMORY_WRITE);
        }
    }

    /*
     * The EPT violation must have been caused by accessing a
     * guest-physical address that is a translation of a guest-linear
     * address.
     */
    if ((ept_qual & EPT_VIOLATION_GLA_VALID) == 0 ||
        (ept_qual & EPT_VIOLATION_XLAT_VALID) == 0) {
        return false;
    }

    return !slot;
}


#endif
