/*
 * Call Logical Processor (CLP) architecture
 *
 * Copyright 2025 IBM Corp.
 * Author(s): Jared Rossi <jrossi@linux.ibm.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "clp.h"
#include <stdio.h>
#include <string.h>

int clp_pci(void *data)
{
    struct { uint8_t _[2048]; } *req = data;
    int cc = 3;

    asm volatile (
        "     .insn   rrf,0xb9a00000,0,%[req],0,2\n"
        "     ipm     %[cc]\n"
        "     srl     %[cc],28\n"
        : [cc] "+d" (cc), "+m" (*req)
        : [req] "a" (req)
        : "cc");
    return cc;
}

/*
 * Get the PCI function entry for a given function ID
 * Return 0 on success, 1 if the FID is not found, or a negative RC on error
 */
int find_pci_function(uint32_t fid, ClpFhListEntry *entry)
{
    int rc;
    int count = 0;
    int limit = PCI_MAX_FUNCTIONS;
    ClpReqRspListPci rrb;

    rrb.request.hdr.len = 32;
    rrb.request.hdr.cmd = 0x02;
    rrb.request.resume_token = 0;
    rrb.response.hdr.len = sizeof(ClpRspListPci);

    do {
        rc = clp_pci(&rrb);
        if (rc) {
            return -rc;
        }

        if (rrb.response.hdr.rsp != 0x0010) {
            printf("Failed to list PCI functions: %x", rrb.response.hdr.rsp);
            return -1;
        }

        /* Resume token set when max enteries are returned */
        if (rrb.response.resume_token) {
            count = CLP_FH_LIST_NR_ENTRIES;
            rrb.request.resume_token = rrb.response.resume_token;
        } else {
            count = (rrb.response.hdr.len - 32) / sizeof(ClpFhListEntry);
        }

        limit -= count;

        for (int i = 0; i < count; i++) {
            if (rrb.response.fh_list[i].fid == fid) {
                memcpy(entry, &rrb.response.fh_list[i], sizeof(ClpFhListEntry));
                return 0;
            }
        }

    } while (rrb.request.resume_token && limit);

    return 1;
}

/*
 * Enable the PCI function associated with a given handle
 * Return 0 on success or a negative RC on error
 */
int enable_pci_function(uint32_t *fhandle)
{
    ClpReqRspSetPci rrb;
    int rc;

    rrb.request.hdr.len = 32;
    rrb.request.hdr.cmd = 0x05;
    rrb.request.fh = *fhandle;
    rrb.request.oc = 0;
    rrb.request.ndas = 1;
    rrb.response.hdr.len = 32;

    rc = clp_pci(&rrb);
    if (rc) {
        return -rc;
    }

    if (rrb.response.hdr.rsp != 0x0010) {
        printf("Failed to enable PCI function: %x", rrb.response.hdr.rsp);
        return -1;
    }

    *fhandle = rrb.response.fh;
    return 0;
}
