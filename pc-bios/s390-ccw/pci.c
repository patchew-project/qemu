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
#include "bswap.h"
#include <stdio.h>
#include <stdbool.h>

/* PCI load */
static inline int pcilg(uint64_t *data, uint64_t req, uint64_t offset, uint8_t *status)
{
    union register_pair req_off = {.even = req, .odd = offset};
    int cc = -1;
    uint64_t __data;

    asm volatile (
        "     .insn   rre,0xb9d20000,%[data],%[req_off]\n"
        "     ipm     %[cc]\n"
        "     srl     %[cc],28\n"
        : [cc] "+d" (cc), [data] "=d" (__data),
          [req_off] "+d" (req_off.pair) :: "cc");
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
        : [cc] "+d" (cc), [req_off] "+d" (req_off.pair)
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

static int pci_write(uint32_t fhandle, uint64_t offset, uint8_t pcias,
                     uint64_t data, uint8_t len)
{

    uint64_t req = ZPCI_CREATE_REQ(fhandle, pcias, len);
    uint8_t status;
    int rc;

    /* writes must be non-zero powers of 2 with a maximum of 8 bytes per read */
    switch (len) {
    case 1:
    case 2:
    case 4:
    case 8:
        rc = pcistg(data, req, offset, &status);
        break;
    default:
        rc = -1;
    }

    /* Error condition detected */
    if (rc == 1) {
        printf("PCI store failed with status condition %d\n", status);
        return -1;
    }

    return rc ? -1 : 0;
}

/* Write an arbitrary length of data without byte swapping */
int pci_write_flex(uint32_t fh, uint64_t offset, uint8_t pcias, void *data, int len)
{
    uint8_t writelen, tmp;
    int rc;
    int remaining = len;

    /* write bytes in powers of 2, up to a maximum of 8 bytes per read */
    while (remaining) {
        if (remaining > 7) {
            writelen = 8;
        } else {
            writelen = 1;
            while (true) {
                tmp = writelen * 2;
                if (tmp > remaining) {
                    break;
                }

                writelen = tmp;
            }
        }

        /* Access next data based on write size */
        switch (writelen) {
        case 1:
            rc = pci_write(fh, offset, pcias, ((uint8_t *)data)[0], 1);
            break;
        case 2:
            rc = pci_write(fh, offset, pcias, ((uint16_t *)data)[0], 2);
            break;
        case 4:
            rc = pci_write(fh, offset, pcias, ((uint32_t *)data)[0], 4);
            break;
        case 8:
            rc = pci_write(fh, offset, pcias, ((uint64_t *)data)[0], 8);
            break;
        default:
            rc = -1;
        }

        if (rc) {
            return -1;
        }

        remaining -= writelen;
        data += writelen;
        offset += writelen;
    }

    return 0;
}

int pci_write_byte(uint32_t fhandle, uint64_t offset, uint8_t pcias, uint8_t data)
{
    return pci_write(fhandle, offset, pcias, (uint64_t)data, 1);
}

/* Wrappers to byte swap common data sizes then write */
int pci_bswap16_write(uint32_t fhandle, uint64_t offset, uint8_t pcias, uint16_t data)
{
    uint64_t le_data = bswap16(data);
    return pci_write(fhandle, offset, pcias, le_data, 2);
}

int pci_bswap32_write(uint32_t fhandle, uint64_t offset, uint8_t pcias, uint32_t data)
{
    uint64_t le_data = bswap32(data);
    return pci_write(fhandle, offset, pcias, le_data, 4);
}

int pci_bswap64_write(uint32_t fhandle, uint64_t offset, uint8_t pcias, uint64_t data)
{
    uint64_t le_data = bswap64(data);
    return pci_write(fhandle, offset, pcias, le_data, 8);
}

static int pci_read(uint32_t fh, uint64_t offset, uint8_t pcias, void *buf, uint8_t len)
{
    uint64_t req, data;
    uint8_t status;
    int rc;

    req = ZPCI_CREATE_REQ(fh, pcias, len);
    rc = pcilg(&data, req, offset, &status);

    /* Error condition detected */
    if (rc == 1) {
        printf("PCI load failed with status condition %d\n", status);
        return -1;
    }

    switch (len) {
    case 1:
        *(uint8_t *)buf = data;
        break;
    case 2:
        *(uint16_t *)buf = data;
        break;
    case 4:
        *(uint32_t *)buf = data;
        break;
    case 8:
        *(uint64_t *)buf = data;
        break;
    default:
        return -1;
    }

    return rc ? -1 : 0;
}

/* Read to an arbitrary length buffer without byte swapping */
int pci_read_flex(uint32_t fh, uint64_t offset, uint8_t pcias, void *buf, int len)
{
    uint8_t readlen, tmp;
    int rc;
    int remaining = len;

    /* Read bytes in powers of 2, up to a maximum of 8 bytes per read */
    while (remaining) {
        if (remaining > 7) {
            readlen = 8;
        } else {
            readlen = 1;
            while (true) {
                tmp = readlen * 2;
                if (tmp > remaining) {
                    break;
                }

                readlen = tmp;
            }
        }

        rc = pci_read(fh, offset, pcias, buf, readlen);
        if (rc) {
            return -1;
        }

        remaining -= readlen;
        buf += readlen;
        offset += readlen;
    }

    return 0;
}

int pci_read_byte(uint32_t fh, uint64_t offset, uint8_t pcias, uint8_t *buf)
{
    return pci_read(fh, offset, pcias, buf, 1);
}

/* Wrappers to read common data sizes then byte swap */
int pci_read_bswap16(uint32_t fh, uint64_t offset, uint8_t pcias, uint16_t *buf)
{
    int rc = pci_read(fh, offset, pcias, buf, 2);
    *buf = bswap16(*buf);
    return rc;
}

int pci_read_bswap32(uint32_t fh, uint64_t offset, uint8_t pcias, uint32_t *buf)
{
    int rc = pci_read(fh, offset, pcias, buf, 4);
    *buf = bswap32(*buf);
    return rc;
}

int pci_read_bswap64(uint32_t fh, uint64_t offset, uint8_t pcias, uint64_t *buf)
{
    int rc = pci_read(fh, offset, pcias, buf, 8);
    *buf = bswap64(*buf);
    return rc;
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
