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

int pci_write(uint32_t fhandle, uint64_t offset, uint64_t data, uint8_t len);
int pci_read(uint32_t fhandle, uint64_t offset, uint8_t picas, void *buf, uint8_t len);
uint8_t find_cap_pos(uint32_t fhandle, uint64_t cfg_type);
int pci_dev_enable(PciDevice *pcidev);
int get_fib(PciFib *fib, uint32_t fhandle);
int set_fib(PciFib *fib, uint32_t fhandle, uint8_t dma_as, uint8_t opcontrol);

#endif
