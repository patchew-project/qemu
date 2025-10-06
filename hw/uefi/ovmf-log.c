/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * print ovmf debug log
 *
 * see OvmfPkg/Library/MemDebugLogLib/ in edk2
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "system/dma.h"
#include "monitor/hmp.h"
#include "monitor/monitor.h"


/* ----------------------------------------------------------------------- */
/* copy from edk2                                                          */

#define MEM_DEBUG_LOG_MAGIC1  0x3167646d666d766f  // "ovmfmdg1"
#define MEM_DEBUG_LOG_MAGIC2  0x3267646d666d766f  // "ovmfmdg2"

/*
 * Mem Debug Log buffer header.
 * The Log buffer is circular. Only the most
 * recent messages are retained. Older messages
 * will be discarded if the buffer overflows.
 * The Debug Log starts just after the header.
 */
typedef struct {
    /*
     * Magic values
     * These fields are used by tools to locate the buffer in
     * memory. These MUST be the first two fields of the structure.
     * Use a 128 bit Magic to vastly reduce the possibility of
     * a collision with random data in memory.
     */
    uint64_t             Magic1;
    uint64_t             Magic2;
    /*
     * Header Size
     * This MUST be the third field of the structure
     */
    uint64_t             HeaderSize;
    /*
     * Debug log size (minus header)
     */
    uint64_t             DebugLogSize;
    /*
     * edk2 uses this for locking access.
     */
    uint64_t             MemDebugLogLock;
    /*
     * Debug log head offset
     */
    uint64_t             DebugLogHeadOffset;
    /*
     *  Debug log tail offset
     */
    uint64_t             DebugLogTailOffset;
    /*
     * Flag to indicate if the buffer wrapped and was thus truncated.
     */
    uint64_t             Truncated;
    /*
     * Firmware Build Version (PcdFirmwareVersionString)
     */
    char                 FirmwareVersion[128];
} MEM_DEBUG_LOG_HDR;


/* ----------------------------------------------------------------------- */
/* qemu monitor command                                                    */

typedef struct {
    uint64_t             Magic1;
    uint64_t             Magic2;
} MEM_DEBUG_LOG_MAGIC;

/* find log buffer in guest memory by searching for the magic cookie */
static dma_addr_t find_ovmf_log(void)
{
    static const MEM_DEBUG_LOG_MAGIC magic = {
        .Magic1 = MEM_DEBUG_LOG_MAGIC1,
        .Magic2 = MEM_DEBUG_LOG_MAGIC2,
    };
    MEM_DEBUG_LOG_MAGIC check;
    dma_addr_t offset;

    /*
     * FIXME: Search range probably need arch specific tweaks for non-x86.
     */
    dma_addr_t start = 0;
    dma_addr_t step = 4 * KiB; /* buffer is page aligned */
    dma_addr_t end = start + 4 * GiB; /* buffer is in low memory */

    for (offset = start; offset < end; offset += step) {
        if (dma_memory_read(&address_space_memory, offset,
                            &check, sizeof(check),
                            MEMTXATTRS_UNSPECIFIED)) {
            /* dma error -> stop searching */
            return -1;
        }
        if (memcmp(&magic, &check, sizeof(check)) == 0) {
            return offset;
        }
    };

    return -1;
}

static void handle_ovmf_log_range(Monitor *mon,
                                  dma_addr_t start,
                                  dma_addr_t end)
{
    g_autofree char *buf = NULL;

    if (start > end) {
        return;
    }

    buf = g_malloc(end - start + 1);
    if (dma_memory_read(&address_space_memory, start,
                        buf, end - start,
                        MEMTXATTRS_UNSPECIFIED)) {
        monitor_printf(mon, "firmware log: buffer read error\n");
        return;
    }

    buf[end - start] = 0;
    monitor_printf(mon, "%s", buf);
}

void hmp_info_ovmf_log(Monitor *mon, const QDict *qdict)
{
    MEM_DEBUG_LOG_HDR header;
    dma_addr_t offset, base;

    offset = find_ovmf_log();
    if (offset == -1) {
        monitor_printf(mon, "firmware log: not found\n");
        return;
    }

    if (dma_memory_read(&address_space_memory, offset,
                        &header, sizeof(header),
                        MEMTXATTRS_UNSPECIFIED)) {
        monitor_printf(mon, "firmware log: header read error\n");
        return;
    }

    monitor_printf(mon, "firmware log: version \"%s\"\n",
                   header.FirmwareVersion);

    base = offset + header.HeaderSize;
    if (header.DebugLogHeadOffset > header.DebugLogTailOffset) {
        /* wrap around */
        handle_ovmf_log_range(mon,
                              base + header.DebugLogHeadOffset,
                              base + header.DebugLogSize);
        handle_ovmf_log_range(mon,
                              base + 0,
                              base + header.DebugLogTailOffset);
    } else {
        handle_ovmf_log_range(mon,
                              base + header.DebugLogHeadOffset,
                              base + header.DebugLogTailOffset);
    }
}
