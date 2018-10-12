/*
 * PCI Host for remote device
 *
 * Copyright 2018, Oracle and/or its affiliates. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef REMOTE_PCIHOST_H
#define REMOTE_PCIHOST_H

#include <stddef.h>
#include <stdint.h>

#include "exec/memory.h"
#include "hw/pci/pcie_host.h"

#define TYPE_REMOTE_HOST_DEVICE "remote-pcihost"
#define REMOTE_HOST_DEVICE(obj) \
    OBJECT_CHECK(RemPCIHost, (obj), TYPE_REMOTE_HOST_DEVICE)

typedef struct RemPCIHost {
    /*< private >*/
    PCIExpressHost parent_obj;
    /*< public >*/

    /* Memory Controller Hub (MCH) may not be necessary for the emulation
     * program. The two important reasons for implementing a PCI host in the
     * emulation program are:
     * - Provide a PCI bus for IO devices
     * - Enable translation of guest PA to the PCI bar regions
     *
     * For both the above mentioned purposes, it doesn't look like we would
     * need the MCH
     */

    MemoryRegion *mr_pci_mem;
    MemoryRegion *mr_sys_mem;
    MemoryRegion *mr_sys_io;
} RemPCIHost;

#endif
