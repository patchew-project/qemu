/*
 * HMAT ACPI Implementation Header
 *
 * Copyright(C) 2019 Intel Corporation.
 *
 * Author:
 *  Liu jingqi <jingqi.liu@linux.intel.com>
 *  Tao Xu <tao3.xu@intel.com>
 *
 * HMAT is defined in ACPI 6.3: 5.2.27 Heterogeneous Memory Attribute Table
 * (HMAT)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#ifndef HMAT_H
#define HMAT_H

#include "hw/acpi/acpi-defs.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/bios-linker-loader.h"
#include "hw/acpi/aml-build.h"

/*
 * ACPI 6.3: 5.2.27.3 Memory Proximity Domain Attributes Structure,
 * Table 5-141, Field "flag", Bit [0]: set to 1 to indicate that data in
 * the Proximity Domain for the Attached Initiator field is valid.
 * Other bits reserved.
 */
#define HMAT_PROX_INIT_VALID 0x1

struct HMAT_LB_Info {
    /*
     * Indicates total number of Proximity Domains
     * that can initiate memory access requests.
     */
    uint32_t    num_initiator;
    /*
     * Indicates total number of Proximity Domains
     * that can act as target.
     */
    uint32_t    num_target;
    /*
     * Indicates it's memory or
     * the specified level memory side cache.
     */
    uint8_t     hierarchy;
    /*
     * Present the type of data,
     * access/read/write latency or bandwidth.
     */
    uint8_t     data_type;
    /* The base unit for latency in nanoseconds. */
    uint64_t    base_lat;
    /* The base unit for bandwidth in megabytes per second(MB/s). */
    uint64_t    base_bw;
    /*
     * latency[i][j]:
     * Indicates the latency based on base_lat
     * from Initiator Proximity Domain i to Target Proximity Domain j.
     */
    uint16_t    latency[MAX_NODES][MAX_NODES];
    /*
     * bandwidth[i][j]:
     * Indicates the bandwidth based on base_bw
     * from Initiator Proximity Domain i to Target Proximity Domain j.
     */
    uint16_t    bandwidth[MAX_NODES][MAX_NODES];
};

struct HMAT_Cache_Info {
    /* The memory proximity domain to which the memory belongs. */
    uint32_t    mem_proximity;
    /* Size of memory side cache in bytes. */
    uint64_t    size;
    /*
     * Total cache levels for this memory
     * pr#include "hw/acpi/aml-build.h"oximity domain.
     */
    uint8_t     total_levels;
    /* Cache level described in this structure. */
    uint8_t     level;
    /* Cache Associativity: None/Direct Mapped/Comple Cache Indexing */
    uint8_t     associativity;
    /* Write Policy: None/Write Back(WB)/Write Through(WT) */
    uint8_t     write_policy;
    /* Cache Line size in bytes. */
    uint16_t    line_size;
};

#define HMAM_MEMORY_SIZE    4096
#define HMAM_MEM_FILE       "etc/acpi/hma-mem"

/*
 * 32 bits IO port starting from 0x0a19 in guest is reserved for
 * HMA ACPI emulation.
 */
#define HMAM_ACPI_IO_BASE     0x0a19
#define HMAM_ACPI_IO_LEN      4

#define HMAM_ACPI_MEM_ADDR  "HMTA"
#define HMAM_MEMORY         "HRAM"
#define HMAM_IOPORT         "HPIO"

#define HMAM_NOTIFY         "NTFI"
#define HMAM_OUT_BUF_SIZE   "RLEN"
#define HMAM_OUT_BUF        "ODAT"

#define HMAM_RHMA_STATUS    "RSTA"
#define HMA_COMMON_METHOD   "HMAC"
#define HMAM_OFFSET         "OFFT"

#define HMAM_RET_STATUS_SUCCESS        0 /* Success */
#define HMAM_RET_STATUS_UNSUPPORT      1 /* Not Supported */
#define HMAM_RET_STATUS_INVALID        2 /* Invalid Input Parameters */
#define HMAM_RET_STATUS_HMA_CHANGED    0x100 /* HMA Changed */

/*
 * HmatHmaBuffer:
 * @hma: HMA buffer with the updated HMAT. It is updated when
 *   the memory device is plugged or unplugged.
 * @dirty: It allows OSPM to detect changes and restart read if there is any.
 */
struct HmatHmaBuffer {
    GArray *hma;
    bool dirty;
};
typedef struct HmatHmaBuffer HmatHmaBuffer;

struct AcpiHmaState {
    /* the data of the fw_cfg file HMAM_MEM_FILE. */
    GArray *hmam_mem;

    HmatHmaBuffer hma_buf;

    /* the IO region used by OSPM to transfer control to QEMU. */
    MemoryRegion io_mr;
};
typedef struct AcpiHmaState AcpiHmaState;

struct HmatHmamIn {
    /* the offset in the _HMA buffer */
    uint32_t offset;
} QEMU_PACKED;
typedef struct HmatHmamIn HmatHmamIn;

struct HmatHmamOut {
    /* the size of buffer filled by QEMU. */
    uint32_t len;
    uint32_t ret_status;   /* return status code. */
    uint8_t data[4088];
} QEMU_PACKED;
typedef struct HmatHmamOut HmatHmamOut;

void build_hmat(GArray *table_data, BIOSLinker *linker, NumaState *nstat);
void hmat_build_aml(Aml *dsdt);
void hmat_init_acpi_state(AcpiHmaState *state, MemoryRegion *io,
                          FWCfgState *fw_cfg, Object *owner);
void hmat_update(NumaState *nstat);

#endif
