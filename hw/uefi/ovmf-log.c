/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * print ovmf debug log
 *
 * see OvmfPkg/Library/MemDebugLogLib/ in edk2
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/target-info-qapi.h"
#include "hw/boards.h"
#include "hw/i386/x86.h"
#include "hw/arm/virt.h"
#include "system/dma.h"
#include "monitor/hmp.h"
#include "monitor/monitor.h"
#include "qapi/error.h"
#include "qapi/type-helpers.h"
#include "qapi/qapi-commands-machine.h"


/* ----------------------------------------------------------------------- */
/* copy from edk2                                                          */

#define MEM_DEBUG_LOG_MAGIC1  0x3167646d666d766f  /* "ovmfmdg1" */
#define MEM_DEBUG_LOG_MAGIC2  0x3267646d666d766f  /* "ovmfmdg2" */

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
static dma_addr_t find_ovmf_log_range(dma_addr_t start, dma_addr_t end)
{
    static const MEM_DEBUG_LOG_MAGIC magic = {
        .Magic1 = MEM_DEBUG_LOG_MAGIC1,
        .Magic2 = MEM_DEBUG_LOG_MAGIC2,
    };
    MEM_DEBUG_LOG_MAGIC check;
    dma_addr_t step = 4 * KiB;
    dma_addr_t offset;

    for (offset = start; offset < end; offset += step) {
        if (dma_memory_read(&address_space_memory, offset,
                            &check, sizeof(check),
                            MEMTXATTRS_UNSPECIFIED)) {
            /* dma error -> stop searching */
            break;
        }
        if (memcmp(&magic, &check, sizeof(check)) == 0) {
            return offset;
        }
    }
    return -1;
}

static dma_addr_t find_ovmf_log(void)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    dma_addr_t start, end, offset;

    if (target_arch() == SYS_EMU_TARGET_X86_64 &&
        object_dynamic_cast(OBJECT(ms), TYPE_X86_MACHINE)) {
        X86MachineState *x86ms = X86_MACHINE(ms);

        /* early log buffer, static allocation in memfd, sec + early pei */
        offset = find_ovmf_log_range(0x800000, 0x900000);
        if (offset != -1) {
            return offset;
        }

        /*
         * normal log buffer, dynamically allocated close to end of low memory,
         * late pei + dxe phase
         */
        end = x86ms->below_4g_mem_size;
        start = end - MIN(end, 128 * MiB);
        offset = find_ovmf_log_range(start, end);
        return offset;
    }

    if (target_arch() == SYS_EMU_TARGET_AARCH64 &&
        object_dynamic_cast(OBJECT(ms), TYPE_VIRT_MACHINE)) {
        /* edk2 ArmVirt firmware allocations are in the first 128 MB */
        VirtMachineState *vms = VIRT_MACHINE(ms);
        start = vms->memmap[VIRT_MEM].base;
        end = start + 128 * MiB;
        offset = find_ovmf_log_range(start, end);
        return offset;
    }

    return -1;
}

static void handle_ovmf_log_range(GString *out,
                                  dma_addr_t start,
                                  dma_addr_t end,
                                  Error **errp)
{
    g_autofree char *buf = NULL;

    if (start > end) {
        return;
    }

    buf = g_malloc(end - start + 1);
    if (dma_memory_read(&address_space_memory, start,
                        buf, end - start,
                        MEMTXATTRS_UNSPECIFIED)) {
        error_setg(errp, "firmware log: buffer read error");
        return;
    }

    buf[end - start] = 0;
    g_string_append_printf(out, "%s", buf);
}

HumanReadableText *qmp_query_ovmf_log(Error **errp)
{
    MEM_DEBUG_LOG_HDR header;
    dma_addr_t offset, base;
    g_autoptr(GString) out = g_string_new("");

    offset = find_ovmf_log();
    if (offset == -1) {
        error_setg(errp, "firmware log: not found");
        goto err;
    }

    if (dma_memory_read(&address_space_memory, offset,
                        &header, sizeof(header),
                        MEMTXATTRS_UNSPECIFIED)) {
        error_setg(errp, "firmware log: header read error");
        goto err;
    }

    if (header.DebugLogSize > MiB) {
        /* default size is 128k (32 pages), allow up to 1M */
        error_setg(errp, "firmware log: log buffer is too big");
        goto err;
    }

    if (header.DebugLogHeadOffset > header.DebugLogSize ||
        header.DebugLogTailOffset > header.DebugLogSize) {
        error_setg(errp, "firmware log: invalid header");
        goto err;
    }

    g_string_append_printf(out, "firmware log: version \"%s\"\n",
                           header.FirmwareVersion);

    base = offset + header.HeaderSize;
    if (header.DebugLogHeadOffset > header.DebugLogTailOffset) {
        /* wrap around */
        handle_ovmf_log_range(out,
                              base + header.DebugLogHeadOffset,
                              base + header.DebugLogSize,
                              errp);
        if (*errp) {
            goto err;
        }
        handle_ovmf_log_range(out,
                              base + 0,
                              base + header.DebugLogTailOffset,
                              errp);
        if (*errp) {
            goto err;
        }
    } else {
        handle_ovmf_log_range(out,
                              base + header.DebugLogHeadOffset,
                              base + header.DebugLogTailOffset,
                              errp);
        if (*errp) {
            goto err;
        }
    }

    return human_readable_text_from_str(out);

err:
    return NULL;
}
