/*
 * s390x PCI funcionality
 *
 * Copyright 2025 IBM Corp.
 * Author(s): Jared Rossi <jrossi@linux.ibm.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "clp.h"
#include "pci.h"
#include <stdio.h>

/* PCI load */
static inline int pcilg(uint64_t *data, uint64_t req, uint64_t offset, uint8_t *status)
{
    union register_pair req_off = {.even = req, .odd = offset};
    int cc = -1;
    uint64_t __data = 0x92;

    asm volatile (
        "     .insn   rre,0xb9d20000,%[data],%[req_off]\n"
        "     ipm     %[cc]\n"
        "     srl     %[cc],28\n"
        : [cc] "+d" (cc), [data] "=d" (__data),
          [req_off] "+&d" (req_off.pair) :: "cc");
    *status = req_off.even >> 24 & 0xff;
    *data = __data;
    return cc;
}

/* PCI store */
int pcistg(uint64_t data, uint64_t req, uint64_t offset, uint8_t *status)
{
    union register_pair req_off = {.even = req, .odd = offset};
    int cc = -1;

    asm volatile (
        "     .insn   rre,0xb9d00000,%[data],%[req_off]\n"
        "     ipm     %[cc]\n"
        "     srl     %[cc],28\n"
        : [cc] "+d" (cc), [req_off] "+&d" (req_off.pair)
        : [data] "d" (data)
        : "cc");
    *status = req_off.even >> 24 & 0xff;
    return cc;
}

/* store PCI function controls */
int stpcifc(uint64_t req, PciFib *fib, uint8_t *status)
{
    uint8_t cc;

    asm volatile (
        "     .insn   rxy,0xe300000000d4,%[req],%[fib]\n"
        "     ipm     %[cc]\n"
        "     srl     %[cc],28\n"
        : [cc] "=d" (cc), [req] "+d" (req), [fib] "+Q" (*fib)
        : : "cc");
    *status = req >> 24 & 0xff;
    return cc;
}

/* modify PCI function controls */
int mpcifc(uint64_t req, PciFib *fib, uint8_t *status)
{
    uint8_t cc;

    asm volatile (
        "     .insn   rxy,0xe300000000d0,%[req],%[fib]\n"
        "     ipm     %[cc]\n"
        "     srl     %[cc],28\n"
        : [cc] "=d" (cc), [req] "+d" (req), [fib] "+Q" (*fib)
        : : "cc");
    *status = req >> 24 & 0xff;
    return cc;
}

int pci_write(uint32_t fhandle, uint64_t offset, uint64_t data, uint8_t len)
{

    uint64_t req = ZPCI_CREATE_REQ(fhandle, 4, len);
    uint8_t status;
    int rc;

    rc = pcistg(data, req, offset, &status);
    if (rc == 1) {
        return status;
    } else if (rc) {
        return rc;
    }

    return 0;
}

int pci_read(uint32_t fhandle, uint64_t offset, uint8_t picas, void *buf, uint8_t len)
{
    uint64_t req;
    uint64_t data;
    uint8_t status;
    int readlen;
    int i = 0;
    int rc = 0;

    while (len > 0 && !rc) {
        data = 0;
        readlen = len > 8 ? 8 : len;
        req = ZPCI_CREATE_REQ(fhandle, picas, readlen);
        rc = pcilg(&data, req, offset + (i * 8), &status);
        ((uint64_t *)buf)[i] = data;
        len -= readlen;
        i++;
    }

    if (rc == 1) {
        return status;
    } else if (rc) {
        return rc;
    }

    return 0;
}

/*
 * Find the position of the capability config within PCI configuration
 * space for a given cfg type.  Return the position if found, otherwise 0.
 */
uint8_t find_cap_pos(uint32_t fhandle, uint64_t cfg_type) {
    uint64_t req, next, cfg;
    uint8_t status;
    int rc;

    req = ZPCI_CREATE_REQ(fhandle, 0xf, 1);
    rc = pcilg(&next, req, PCI_CAPABILITY_LIST, &status);
    rc = pcilg(&cfg, req, next + 3, &status);

    while (!rc && (cfg != cfg_type) && next) {
        rc = pcilg(&next, req, next + 1, &status);
        rc = pcilg(&cfg, req, next + 3, &status);
    }

    return rc ? 0 : next;
}

int pci_dev_enable(PciDevice *pcidev)
{
    int rc;

    rc = enable_pci_function(&pcidev->fhandle);
    if (rc) {
        return rc;
    }

    pcidev->status = PCIST_ENABLED;

    return get_fib(&pcidev->fib, pcidev->fhandle);
}

int get_fib(PciFib *fib, uint32_t fhandle)
{
    uint64_t req = ZPCI_CREATE_REQ(fhandle, 0, 0);
    uint8_t status;
    int rc;

    rc = stpcifc(req, fib, &status);

    if (rc == 1) {
        return status;
    } else if (rc) {
        return rc;
    }

    return 0;
}

int set_fib(PciFib *fib, uint32_t fhandle, uint8_t dma_as, uint8_t opcontrol)
{
    uint64_t req = ZPCI_CREATE_REQ(fhandle, dma_as, opcontrol);
    uint8_t status;
    int rc;

    rc = mpcifc(req, fib, &status);

    if (rc == 1) {
        return status;
    } else if (rc) {
        return rc;
    }

    return 0;
}
