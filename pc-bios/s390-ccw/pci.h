/*
 * s390x PCI definitions
 *
 * Copyright 2025 IBM Corp.
 * Author(s): Jared Rossi <jrossi@linux.ibm.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PCI_H
#define PCI_H

#include <stdint.h>
#include <stdbool.h>
#include "clp.h"

#define ZPCI_CREATE_REQ(handle, space, len)                    \
    ((uint64_t) handle << 32 | space << 16 | len)

union register_pair {
    unsigned __int128 pair;
    struct {
        unsigned long even;
        unsigned long odd;
    };
};

#define PCIFIB_FC_ENABLED      0x80
#define PCIFIB_FC_ERROR        0x40
#define PCIFIB_FC_BLOCKED      0x20
#define PCIFIB_FC_DMAREG       0x10

#define PCIST_DISABLED         0x0
#define PCIST_ENABLED          0x1

#define PCI_CFGBAR             0xF  /* Base Address Register for config space */
#define PCI_CAPABILITY_LIST    0x34 /* Offset of first capability list entry */

struct PciFib {
    uint32_t reserved0[2];
    uint8_t fcflags;
    uint8_t reserved1[3];
    uint32_t reserved2;
    uint64_t pba;
    uint64_t pal;
    uint64_t iota;
    uint16_t isc:4;
    uint16_t noi:12;
    uint8_t reserved3:2;
    uint8_t aibvo:6;
    uint8_t s:1;
    uint8_t reserved4:1;
    uint8_t aisbo:6;
    uint32_t reserved5;
    uint64_t aibv;
    uint64_t aisb;
    uint64_t fmba;
    uint32_t reserved6[2];
};
typedef struct PciFib PciFib;

struct PciDevice {
    uint16_t device_id;
    uint16_t vendor_id;
    uint32_t fid;
    uint32_t fhandle;
    uint8_t status;
    PciFib fib;
};
typedef struct PciDevice PciDevice;

int pci_write_flex(uint32_t fhandle, uint64_t offset, uint8_t pcias, void *data, int len);
int pci_write_byte(uint32_t fhandle, uint64_t offset, uint8_t pcias, uint8_t data);
int pci_bswap16_write(uint32_t fhandle, uint64_t offset, uint8_t pcias, uint16_t data);
int pci_bswap32_write(uint32_t fhandle, uint64_t offset, uint8_t pcias, uint32_t data);
int pci_bswap64_write(uint32_t fhandle, uint64_t offset, uint8_t pcias, uint64_t data);

int pci_read_flex(uint32_t fhandle, uint64_t offset, uint8_t pcias, void *buf, int len);
int pci_read_bswap64(uint32_t fhandle, uint64_t offset, uint8_t pcias, uint64_t *buf);
int pci_read_bswap32(uint32_t fhandle, uint64_t offset, uint8_t pcias, uint32_t *buf);
int pci_read_bswap16(uint32_t fhandle, uint64_t offset, uint8_t pcias, uint16_t *buf);
int pci_read_byte(uint32_t fhandle, uint64_t offset, uint8_t pcias, uint8_t *buf);

int pci_dev_enable(PciDevice *pcidev);
int get_fib(PciFib *fib, uint32_t fhandle);
int set_fib(PciFib *fib, uint32_t fhandle, uint8_t dma_as, uint8_t opcontrol);

#endif
