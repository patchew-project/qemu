/*
 * Phytium E2000 MHU/SCMI doorbell
 *
 * This models the PBF firmware transport and the SCMI Base protocol used by
 * Linux. Protocols whose platform data is not available remain unsupported.
 *
 * Copyright (c) 2026 Process Mission
 *
 * Author:
 *   Bin Meng <bin.meng@processmission.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/misc/phytium_e2000_mhu.h"

#include "hw/core/irq.h"
#include "hw/core/registerfields.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "system/address-spaces.h"
#include "target/arm/arm-powerctl.h"
#include "target/arm/cpu.h"

#define PHYTIUM_E2000_PBF_SCMI_MBOX_BASE     0x32a10400
#define PHYTIUM_E2000_OS_SCMI_MBOX_BASE      0x32a11400
#define PHYTIUM_E2000_OS_SCMI_MBOX_SIZE      0x400
#define PHYTIUM_E2000_SCMI_STATUS_OFFSET     0x04
#define PHYTIUM_E2000_SCMI_FLAGS_OFFSET      0x10
#define PHYTIUM_E2000_SCMI_LEN_OFFSET        0x14
#define PHYTIUM_E2000_SCMI_HEADER_OFFSET     0x18
#define PHYTIUM_E2000_SCMI_PAYLOAD_OFFSET    0x1c
#define PHYTIUM_E2000_SCMI_STATUS_FREE       BIT(0)
#define PHYTIUM_E2000_SCMI_FLAG_INTR_ENABLED BIT(0)
#define PHYTIUM_E2000_SCMI_MAX_FRAME_SIZE    \
    (PHYTIUM_E2000_OS_SCMI_MBOX_SIZE - \
     PHYTIUM_E2000_SCMI_HEADER_OFFSET)

#define SCMI_MESSAGE_ID(header)              extract32((header), 0, 8)
#define SCMI_MESSAGE_TYPE(header)            extract32((header), 8, 2)
#define SCMI_PROTOCOL_ID(header)             extract32((header), 10, 8)
#define SCMI_RESERVED(header)                extract32((header), 28, 4)
#define SCMI_MESSAGE_TYPE_COMMAND            0
#define SCMI_PROTOCOL_BASE                   0x10
#define SCMI_PROTOCOL_POWER_DOMAIN           0x11
#define SCMI_PROTOCOL_PHYTIUM                0x81
#define SCMI_PROTOCOL_VERSION                0x0
#define SCMI_PROTOCOL_ATTRIBUTES             0x1
#define SCMI_PROTOCOL_MESSAGE_ATTRIBUTES     0x2
#define SCMI_BASE_DISCOVER_VENDOR            0x3
#define SCMI_BASE_DISCOVER_SUB_VENDOR        0x4
#define SCMI_BASE_DISCOVER_IMPLEMENT_VERSION 0x5
#define SCMI_BASE_DISCOVER_LIST_PROTOCOLS    0x6
#define SCMI_BASE_DISCOVER_AGENT             0x7
#define SCMI_BASE_NOTIFY_ERRORS              0x8
#define SCMI_POWER_STATE_SET                 0x4
#define SCMI_PHYTIUM_GET_PSOSTAT             0x3
#define SCMI_POWER_STATE_TYPE                BIT(30)
#define SCMI_POWER_STATE_ID_MASK             (SCMI_POWER_STATE_TYPE - 1)
/* Values reported by the E2000 SCP firmware */
#define SCMI_BASE_VERSION                    0x00020000
#define SCMI_BASE_IMPLEMENTATION_VERSION     0x02050000
#define SCMI_BASE_NUM_PROTOCOLS              0
#define SCMI_BASE_NUM_AGENTS                 2
#define SCMI_DOORBELL_COMPLETE               BIT(31)

#define SCMI_SUCCESS            0
#define SCMI_NOT_SUPPORTED      (-1)
#define SCMI_INVALID_PARAMETERS (-2)
#define SCMI_NOT_FOUND          (-4)
#define SCMI_GENERIC_ERROR      (-8)
#define SCMI_PROTOCOL_ERROR     (-10)

#define PHYTIUM_E2000_PBF_ROOT_ANCHOR       \
    (PHYTIUM_E2000_PBR_BOOT_SRAM_BASE + 0xf00)
#define PHYTIUM_E2000_CPU_TARGET_OFFSET     0x08
#define PHYTIUM_E2000_CPU_LOCK_DEPTH_OFFSET 0x28
#define PHYTIUM_E2000_CPU_LOCK_OWNER_OFFSET 0x30
#define PHYTIUM_E2000_CPU_ON_COMPLETE       0xabcdef98

REG32(AP_RX_STAT, 0x000)
REG32(AP_RX_SET, 0x008)
REG32(AP_RX_CLR, 0x010)
REG32(AP_TX_STAT, 0x100)
REG32(AP_TX_SET, 0x108)
REG32(AP_TX_CLR, 0x110)

/* PBF selects the AP OS channel at offset 0x200 */
REG32(AP_OS_STAT, 0x300)
REG32(AP_OS_SET, 0x308)
REG32(AP_OS_CLR, 0x310)

#define PHYTIUM_E2000_MHU_R_MAX \
    (PHYTIUM_E2000_MHU_MMIO_SIZE / sizeof(uint32_t))

struct PhytiumE2000MHUState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;
    uint32_t regs[PHYTIUM_E2000_MHU_R_MAX];
    uint64_t cpu_mpidrs[PHYTIUM_E2000_MHU_MAX_CPUS];
    CPUState *cpus[PHYTIUM_E2000_MHU_MAX_CPUS];
    /* Firmware-owned slot address supplied by the PBR before realization */
    hwaddr secondary_vector_slot;
    unsigned int num_cpus;
};

static bool phytium_e2000_phys_readl(hwaddr addr, uint32_t *value)
{
    uint8_t buf[sizeof(*value)];

    if (address_space_read(&address_space_memory, addr,
                           MEMTXATTRS_UNSPECIFIED, buf,
                           sizeof(buf)) != MEMTX_OK) {
        return false;
    }
    *value = ldl_le_p(buf);
    return true;
}

static bool phytium_e2000_phys_readq(hwaddr addr, uint64_t *value)
{
    uint8_t buf[sizeof(*value)];

    if (address_space_read(&address_space_memory, addr,
                           MEMTXATTRS_UNSPECIFIED, buf,
                           sizeof(buf)) != MEMTX_OK) {
        return false;
    }
    *value = ldq_le_p(buf);
    return true;
}

static bool phytium_e2000_phys_writel(hwaddr addr, uint32_t value)
{
    uint8_t buf[sizeof(value)];

    stl_le_p(buf, value);
    return address_space_write(&address_space_memory, addr,
                               MEMTXATTRS_UNSPECIFIED, buf,
                               sizeof(buf)) == MEMTX_OK;
}

static bool phytium_e2000_phys_writeq(hwaddr addr, uint64_t value)
{
    uint8_t buf[sizeof(value)];

    stq_le_p(buf, value);
    return address_space_write(&address_space_memory, addr,
                               MEMTXATTRS_UNSPECIFIED, buf,
                               sizeof(buf)) == MEMTX_OK;
}

static bool phytium_e2000_phys_write(hwaddr addr, const void *buf, size_t size)
{
    return address_space_write(&address_space_memory, addr,
                               MEMTXATTRS_UNSPECIFIED, buf, size) == MEMTX_OK;
}

static uint32_t phytium_e2000_scmi_readl(hwaddr base, hwaddr offset)
{
    uint32_t value = 0;

    phytium_e2000_phys_readl(base + offset, &value);
    return value;
}

static void phytium_e2000_scmi_writel(hwaddr base, hwaddr offset,
                                      uint32_t value)
{
    phytium_e2000_phys_writel(base + offset, value);
}

static void phytium_e2000_scmi_publish(hwaddr base, uint32_t len)
{
    phytium_e2000_scmi_writel(base, PHYTIUM_E2000_SCMI_LEN_OFFSET, len);
    /*
     * Publish the free bit last. PBF polls this field as the ownership handoff
     * and may consume the response immediately after observing it.
     */
    phytium_e2000_scmi_writel(base, PHYTIUM_E2000_SCMI_STATUS_OFFSET,
                              PHYTIUM_E2000_SCMI_STATUS_FREE);
}

static bool phytium_e2000_mhu_cpu_is_on(PhytiumE2000MHUState *s,
                                        uint64_t mpidr)
{
    unsigned int i;

    for (i = 0; i < s->num_cpus; i++) {
        if ((s->cpu_mpidrs[i] & 0xffff) == (mpidr & 0xffff)) {
            return ARM_CPU(s->cpus[i])->power_state == PSCI_ON;
        }
    }

    return false;
}

static bool phytium_e2000_mhu_prepare_cpu_on(PhytiumE2000MHUState *s,
                                             uint64_t mpidr,
                                             uint64_t *runtime_cpu_control,
                                             uint64_t *secondary_entry)
{
    uint64_t pbr_cpu_control;
    uint64_t runtime_root;
    uint64_t target;
    uint64_t magic;
    uint32_t first_instruction;
    uint32_t lock_depth;
    uint32_t lock_owner;

    /*
     * BL1 and EL3 deliberately use different roots after PBF relocates the
     * runtime object graph. BL1's reset trampoline follows the PBR-owned root
     * at 0x30c01000, while EL3 follows the relocatable anchor at 0x30c00f00.
     * The emulated SCP therefore copies the requested MPIDR into BL1's
     * control block before releasing the secondary CPU.
     *
     * The secondary-vector slot itself was recovered and validated while PBR
     * parsed BL1.  Read the slot for every CPU_ON request rather than caching
     * its contents: BL1 first publishes the resident EL3 entry at runtime and
     * the temporary BL1 mapping may subsequently be overwritten.
     */
    if (!phytium_e2000_phys_readq(PHYTIUM_E2000_PBR_ROOT,
                                  &pbr_cpu_control) ||
        pbr_cpu_control != PHYTIUM_E2000_PBR_CPU_CONTROL ||
        !phytium_e2000_phys_readq(pbr_cpu_control, &magic) ||
        magic != PHYTIUM_E2000_PBR_CPU_CONTROL_MAGIC ||
        !phytium_e2000_phys_readq(PHYTIUM_E2000_PBF_ROOT_ANCHOR,
                                  &runtime_root) ||
        runtime_root == PHYTIUM_E2000_PBR_ROOT ||
        !QEMU_IS_ALIGNED(runtime_root, sizeof(uint64_t)) ||
        !phytium_e2000_phys_readq(runtime_root, runtime_cpu_control) ||
        !QEMU_IS_ALIGNED(*runtime_cpu_control, sizeof(uint64_t)) ||
        *runtime_cpu_control < PHYTIUM_E2000_PBR_BOOT_SRAM_BASE ||
        *runtime_cpu_control > PHYTIUM_E2000_PBR_BOOT_SRAM_BASE +
                               PHYTIUM_E2000_PBR_BOOT_SRAM_SIZE -
                               (PHYTIUM_E2000_CPU_LOCK_OWNER_OFFSET +
                                sizeof(uint32_t)) ||
        !phytium_e2000_phys_readq(*runtime_cpu_control +
                                  PHYTIUM_E2000_CPU_TARGET_OFFSET,
                                  &target) ||
        (target & 0xffff) != (mpidr & 0xffff) ||
        !phytium_e2000_phys_readl(*runtime_cpu_control +
                                  PHYTIUM_E2000_CPU_LOCK_DEPTH_OFFSET,
                                  &lock_depth) ||
        !phytium_e2000_phys_readl(*runtime_cpu_control +
                                  PHYTIUM_E2000_CPU_LOCK_OWNER_OFFSET,
                                  &lock_owner) ||
        !s->secondary_vector_slot ||
        !phytium_e2000_phys_readq(s->secondary_vector_slot,
                                  secondary_entry) ||
        !QEMU_IS_ALIGNED(*secondary_entry, sizeof(uint32_t)) ||
        !phytium_e2000_phys_readl(*secondary_entry, &first_instruction) ||
        first_instruction == 0 || first_instruction == UINT32_MAX) {
        return false;
    }

    /*
     * EL3 records three nested power-domain lock levels for the first CPU_ON.
     * Later requests observe the already retired zero state. The lock owner
     * must name a CPU which is currently powered on.
     */
    if (!((lock_depth == 3 &&
           phytium_e2000_mhu_cpu_is_on(s, lock_owner)) ||
          (lock_depth == 0 && lock_owner == 0))) {
        return false;
    }

    return phytium_e2000_phys_writeq(
        pbr_cpu_control + PHYTIUM_E2000_CPU_TARGET_OFFSET, mpidr & 0xffff);
}

static bool phytium_e2000_mhu_complete_cpu_on(uint64_t runtime_cpu_control)
{
    /*
     * EL3 polls its relocated control block for 0xabcdef98 after issuing the
     * SCMI request. The secondary's on-finish hook begins by acquiring the
     * same reentrant lock and writes the completion value only afterwards.
     * The power-controller handoff must therefore retire the primary's lock
     * state before publishing completion, or both CPUs wait on each other.
     *
     * This ordering and the offsets were recovered from the Phytium Pi and
     * COMe SDK BL1/EL3 binaries; they are not described by the published PBF
     * ABI.
     */
    return phytium_e2000_phys_writel(
               runtime_cpu_control + PHYTIUM_E2000_CPU_LOCK_DEPTH_OFFSET, 0) &&
           phytium_e2000_phys_writel(
               runtime_cpu_control + PHYTIUM_E2000_CPU_LOCK_OWNER_OFFSET, 0) &&
           phytium_e2000_phys_writeq(
               runtime_cpu_control + PHYTIUM_E2000_CPU_TARGET_OFFSET,
               PHYTIUM_E2000_CPU_ON_COMPLETE);
}

static uint32_t phytium_e2000_mhu_psostat(PhytiumE2000MHUState *s)
{
    uint32_t status = 0;
    unsigned int i;

    /*
     * Phytium PBF's vendor SCMI query returns two bits per E2000 core in the
     * SoC's physical CPU order. A value of 2 denotes powered off and 0
     * denotes powered on. This produces the 0x8a reset value observed on
     * hardware when MPIDR 0x200 is the only running core.
     */
    for (i = 0; i < s->num_cpus; i++) {
        if (ARM_CPU(s->cpus[i])->power_state != PSCI_ON) {
            status |= 2U << (2 * i);
        }
    }

    return status;
}

static int32_t phytium_e2000_mhu_set_power_state(PhytiumE2000MHUState *s)
{
    uint32_t domain_id = phytium_e2000_scmi_readl(
        PHYTIUM_E2000_PBF_SCMI_MBOX_BASE,
        PHYTIUM_E2000_SCMI_PAYLOAD_OFFSET + 4);
    uint32_t power_state = phytium_e2000_scmi_readl(
        PHYTIUM_E2000_PBF_SCMI_MBOX_BASE,
        PHYTIUM_E2000_SCMI_PAYLOAD_OFFSET + 8);
    uint32_t core_mask = power_state & SCMI_POWER_STATE_ID_MASK;
    bool power_on = power_state & SCMI_POWER_STATE_TYPE;
    uint64_t runtime_cpu_control;
    uint64_t secondary_entry;
    unsigned int i;
    int ret;

    /*
     * The E2000 firmware encodes Aff1 as the SCMI power domain and a single
     * Aff0 bit in the vendor power-state ID. This relation is visible in the
     * PBF request builder: MPIDR 0x201 becomes domain 2, state 0x40000002;
     * MPIDR 0x100 becomes domain 1, state 0x40000001.
     */
    if (!is_power_of_2(core_mask)) {
        return SCMI_INVALID_PARAMETERS;
    }

    for (i = 0; i < s->num_cpus; i++) {
        uint64_t mpidr = s->cpu_mpidrs[i];
        unsigned int aff0 = extract64(mpidr, 0, 8);
        unsigned int aff1 = extract64(mpidr, 8, 8);

        if (aff0 >= 30 || aff1 != domain_id || BIT(aff0) != core_mask) {
            continue;
        }

        if (power_on) {
            if (!phytium_e2000_mhu_prepare_cpu_on(
                    s, mpidr, &runtime_cpu_control, &secondary_entry)) {
                return SCMI_GENERIC_ERROR;
            }
            /*
             * BL1 publishes the resident EL3 secondary entry in a fixed
             * vector slot. Some firmware reuses the temporary BL1 image
             * before Linux requests CPU_ON, so reset directly into the
             * published resident entry rather than a BL1 flash offset.
             */
            object_property_set_int(OBJECT(s->cpus[i]), "rvbar",
                                    secondary_entry, &error_abort);
            ret = arm_set_cpu_on_and_reset(mpidr);
            if (ret == QEMU_ARM_POWERCTL_RET_SUCCESS &&
                !phytium_e2000_mhu_complete_cpu_on(runtime_cpu_control)) {
                return SCMI_GENERIC_ERROR;
            }
        } else {
            ret = arm_set_cpu_off(mpidr);
        }
        return ret == QEMU_ARM_POWERCTL_RET_SUCCESS ?
               SCMI_SUCCESS : SCMI_GENERIC_ERROR;
    }

    return SCMI_INVALID_PARAMETERS;
}

static void phytium_e2000_mhu_complete_scmi(PhytiumE2000MHUState *s)
{
    uint32_t header = phytium_e2000_scmi_readl(
        PHYTIUM_E2000_PBF_SCMI_MBOX_BASE,
        PHYTIUM_E2000_SCMI_HEADER_OFFSET);
    uint32_t len = MAX(phytium_e2000_scmi_readl(
        PHYTIUM_E2000_PBF_SCMI_MBOX_BASE,
        PHYTIUM_E2000_SCMI_LEN_OFFSET), (uint32_t)sizeof(uint32_t));
    int32_t scmi_status = SCMI_SUCCESS;

    /*
     * PBF issues clock and platform setup commands whose side effects do not
     * affect modeled devices. Preserve their payload length and acknowledge
     * them; only messages that change modeled CPU state need special handling.
     */
    if (SCMI_PROTOCOL_ID(header) == SCMI_PROTOCOL_PHYTIUM &&
        SCMI_MESSAGE_ID(header) == SCMI_PHYTIUM_GET_PSOSTAT) {
        phytium_e2000_scmi_writel(PHYTIUM_E2000_PBF_SCMI_MBOX_BASE,
                                  PHYTIUM_E2000_SCMI_PAYLOAD_OFFSET + 4,
                                  phytium_e2000_mhu_psostat(s));
        len = 3 * sizeof(uint32_t);
    } else if (SCMI_PROTOCOL_ID(header) == SCMI_PROTOCOL_POWER_DOMAIN &&
               SCMI_MESSAGE_ID(header) == SCMI_POWER_STATE_SET) {
        scmi_status = phytium_e2000_mhu_set_power_state(s);
        len = 2 * sizeof(uint32_t);
    }

    phytium_e2000_scmi_writel(PHYTIUM_E2000_PBF_SCMI_MBOX_BASE,
                              PHYTIUM_E2000_SCMI_PAYLOAD_OFFSET,
                              scmi_status);
    phytium_e2000_scmi_publish(PHYTIUM_E2000_PBF_SCMI_MBOX_BASE, len);
}

static bool phytium_e2000_scmi_base_message_supported(uint32_t message_id)
{
    return message_id <= SCMI_BASE_DISCOVER_AGENT;
}

static int32_t phytium_e2000_mhu_complete_base(uint32_t message_id,
                                               uint32_t request_len,
                                               uint8_t response[16],
                                               size_t *response_len)
{
    uint32_t parameter;

    *response_len = 0;

    switch (message_id) {
    case SCMI_PROTOCOL_VERSION:
        if (request_len != sizeof(uint32_t)) {
            return SCMI_PROTOCOL_ERROR;
        }
        stl_le_p(response, SCMI_BASE_VERSION);
        *response_len = sizeof(uint32_t);
        return SCMI_SUCCESS;
    case SCMI_PROTOCOL_ATTRIBUTES:
        if (request_len != sizeof(uint32_t)) {
            return SCMI_PROTOCOL_ERROR;
        }
        response[0] = SCMI_BASE_NUM_PROTOCOLS;
        response[1] = SCMI_BASE_NUM_AGENTS;
        *response_len = sizeof(uint32_t);
        return SCMI_SUCCESS;
    case SCMI_PROTOCOL_MESSAGE_ATTRIBUTES:
        if (request_len != 2 * sizeof(uint32_t) ||
            !phytium_e2000_phys_readl(
                PHYTIUM_E2000_OS_SCMI_MBOX_BASE +
                PHYTIUM_E2000_SCMI_PAYLOAD_OFFSET, &parameter)) {
            return SCMI_PROTOCOL_ERROR;
        }
        if (!phytium_e2000_scmi_base_message_supported(parameter)) {
            return SCMI_NOT_FOUND;
        }
        stl_le_p(response, 0);
        *response_len = sizeof(uint32_t);
        return SCMI_SUCCESS;
    case SCMI_BASE_DISCOVER_VENDOR:
        if (request_len != sizeof(uint32_t)) {
            return SCMI_PROTOCOL_ERROR;
        }
        memcpy(response, "Phytium", sizeof("Phytium"));
        *response_len = 16;
        return SCMI_SUCCESS;
    case SCMI_BASE_DISCOVER_SUB_VENDOR:
        if (request_len != sizeof(uint32_t)) {
            return SCMI_PROTOCOL_ERROR;
        }
        memcpy(response, "E2000", sizeof("E2000"));
        *response_len = 16;
        return SCMI_SUCCESS;
    case SCMI_BASE_DISCOVER_IMPLEMENT_VERSION:
        if (request_len != sizeof(uint32_t)) {
            return SCMI_PROTOCOL_ERROR;
        }
        stl_le_p(response, SCMI_BASE_IMPLEMENTATION_VERSION);
        *response_len = sizeof(uint32_t);
        return SCMI_SUCCESS;
    case SCMI_BASE_DISCOVER_LIST_PROTOCOLS:
        if (request_len != 2 * sizeof(uint32_t) ||
            !phytium_e2000_phys_readl(
                PHYTIUM_E2000_OS_SCMI_MBOX_BASE +
                PHYTIUM_E2000_SCMI_PAYLOAD_OFFSET, &parameter)) {
            return SCMI_PROTOCOL_ERROR;
        }
        if (parameter) {
            return SCMI_INVALID_PARAMETERS;
        }
        stl_le_p(response, 0);
        *response_len = sizeof(uint32_t);
        return SCMI_SUCCESS;
    case SCMI_BASE_DISCOVER_AGENT:
        if (request_len != 2 * sizeof(uint32_t) ||
            !phytium_e2000_phys_readl(
                PHYTIUM_E2000_OS_SCMI_MBOX_BASE +
                PHYTIUM_E2000_SCMI_PAYLOAD_OFFSET, &parameter)) {
            return SCMI_PROTOCOL_ERROR;
        }
        if (parameter > SCMI_BASE_NUM_AGENTS - 1) {
            return SCMI_NOT_FOUND;
        }
        /*
         * The E2000 SCP advertises Base v2.0 but returns the v1 16-byte agent
         * name. Match this quirk because the SDK 5.10 client parses that
         * layout rather than the v2 agent-ID-plus-name response.
         */
        if (parameter == 0) {
            memcpy(response, "platform", sizeof("platform"));
        } else {
            memcpy(response, "OSPM", sizeof("OSPM"));
        }
        *response_len = 16;
        return SCMI_SUCCESS;
    case SCMI_BASE_NOTIFY_ERRORS:
    default:
        return SCMI_NOT_SUPPORTED;
    }
}

static bool phytium_e2000_mhu_publish_os_response(uint32_t header,
                                                  int32_t status,
                                                  const uint8_t *response,
                                                  size_t response_len)
{
    hwaddr payload = PHYTIUM_E2000_OS_SCMI_MBOX_BASE +
                     PHYTIUM_E2000_SCMI_PAYLOAD_OFFSET;

    if (!phytium_e2000_phys_writel(payload, status) ||
        (response_len &&
         !phytium_e2000_phys_write(payload + sizeof(uint32_t), response,
                                   response_len)) ||
        !phytium_e2000_phys_writel(
            PHYTIUM_E2000_OS_SCMI_MBOX_BASE +
            PHYTIUM_E2000_SCMI_HEADER_OFFSET, header) ||
        !phytium_e2000_phys_writel(
            PHYTIUM_E2000_OS_SCMI_MBOX_BASE +
            PHYTIUM_E2000_SCMI_LEN_OFFSET,
            2 * sizeof(uint32_t) + response_len) ||
        !phytium_e2000_phys_writel(
            PHYTIUM_E2000_OS_SCMI_MBOX_BASE +
            PHYTIUM_E2000_SCMI_STATUS_OFFSET,
            PHYTIUM_E2000_SCMI_STATUS_FREE)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "phytium-e2000-mhu: failed to publish SCMI response\n");
        return false;
    }

    return true;
}

static bool phytium_e2000_mhu_complete_os_scmi(PhytiumE2000MHUState *s)
{
    uint8_t response[16] = { 0 };
    uint32_t flags;
    uint32_t header;
    uint32_t len;
    size_t response_len = 0;
    int32_t status;

    if (!phytium_e2000_phys_readl(
            PHYTIUM_E2000_OS_SCMI_MBOX_BASE +
            PHYTIUM_E2000_SCMI_LEN_OFFSET, &len) ||
        !phytium_e2000_phys_readl(
            PHYTIUM_E2000_OS_SCMI_MBOX_BASE +
            PHYTIUM_E2000_SCMI_HEADER_OFFSET, &header) ||
        !phytium_e2000_phys_readl(
            PHYTIUM_E2000_OS_SCMI_MBOX_BASE +
            PHYTIUM_E2000_SCMI_FLAGS_OFFSET, &flags)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "phytium-e2000-mhu: failed to read SCMI request\n");
        return false;
    }

    if (len < sizeof(uint32_t) || len > PHYTIUM_E2000_SCMI_MAX_FRAME_SIZE ||
        !QEMU_IS_ALIGNED(len, sizeof(uint32_t)) ||
        (flags & ~PHYTIUM_E2000_SCMI_FLAG_INTR_ENABLED) ||
        SCMI_MESSAGE_TYPE(header) != SCMI_MESSAGE_TYPE_COMMAND ||
        SCMI_RESERVED(header)) {
        status = SCMI_PROTOCOL_ERROR;
    } else if (SCMI_PROTOCOL_ID(header) != SCMI_PROTOCOL_BASE) {
        status = SCMI_NOT_SUPPORTED;
    } else {
        status = phytium_e2000_mhu_complete_base(
            SCMI_MESSAGE_ID(header), len, response, &response_len);
    }

    g_assert(response_len <= sizeof(response));
    if (!phytium_e2000_mhu_publish_os_response(
            header, status, response, response_len)) {
        return false;
    }

    /*
     * The vendor mailbox poll callback recognizes only BIT(31) as transmit
     * completion. Raise RX only when the requester selected interrupts.
     */
    s->regs[A_AP_TX_STAT / sizeof(uint32_t)] = SCMI_DOORBELL_COMPLETE;
    if (flags & PHYTIUM_E2000_SCMI_FLAG_INTR_ENABLED) {
        s->regs[A_AP_RX_STAT / sizeof(uint32_t)] |=
            SCMI_DOORBELL_COMPLETE;
        qemu_set_irq(s->irq, 1);
    }

    return true;
}

void phytium_e2000_mhu_seed_mailbox(void)
{
    /*
     * PBR leaves the shared channel available before releasing PBF. Seed the
     * same ownership and success state even before the first doorbell write.
     */
    phytium_e2000_scmi_writel(PHYTIUM_E2000_PBF_SCMI_MBOX_BASE,
                              PHYTIUM_E2000_SCMI_PAYLOAD_OFFSET,
                              SCMI_SUCCESS);
    phytium_e2000_scmi_publish(PHYTIUM_E2000_PBF_SCMI_MBOX_BASE,
                               sizeof(uint32_t));
}

static void phytium_e2000_mhu_update_irq(PhytiumE2000MHUState *s)
{
    qemu_set_irq(s->irq,
                 s->regs[A_AP_RX_STAT / sizeof(uint32_t)] != 0);
}

static uint64_t phytium_e2000_mhu_read(void *opaque, hwaddr offset,
                                       unsigned size)
{
    PhytiumE2000MHUState *s = PHYTIUM_E2000_MHU(opaque);

    switch (offset) {
    case A_AP_RX_STAT:
    case A_AP_TX_STAT:
    case A_AP_OS_STAT:
    case A_AP_OS_SET:
    case A_AP_OS_CLR:
        return s->regs[offset / sizeof(uint32_t)];
    case A_AP_RX_SET:
    case A_AP_RX_CLR:
    case A_AP_TX_SET:
    case A_AP_TX_CLR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "phytium-e2000-mhu: read from write-only register "
                      "at 0x%" HWADDR_PRIx "\n", offset);
        return 0;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "phytium-e2000-mhu: read from unimplemented register "
                      "at 0x%" HWADDR_PRIx "\n", offset);
        return 0;
    }
}

static void phytium_e2000_mhu_write(void *opaque, hwaddr offset,
                                    uint64_t value, unsigned size)
{
    PhytiumE2000MHUState *s = PHYTIUM_E2000_MHU(opaque);
    uint32_t val = value;

    switch (offset) {
    case A_AP_RX_SET:
        s->regs[A_AP_RX_STAT / sizeof(uint32_t)] |= val;
        phytium_e2000_mhu_update_irq(s);
        break;
    case A_AP_RX_CLR:
        s->regs[A_AP_RX_STAT / sizeof(uint32_t)] &= ~val;
        phytium_e2000_mhu_update_irq(s);
        break;
    case A_AP_TX_SET:
        if (val) {
            s->regs[A_AP_TX_STAT / sizeof(uint32_t)] |= val;
            phytium_e2000_mhu_complete_os_scmi(s);
        }
        break;
    case A_AP_TX_CLR:
        s->regs[A_AP_TX_STAT / sizeof(uint32_t)] &= ~val;
        break;
    case A_AP_OS_SET:
        s->regs[A_AP_OS_SET / sizeof(uint32_t)] = val;
        if (val) {
            phytium_e2000_mhu_complete_scmi(s);
        }
        break;
    case A_AP_OS_CLR:
        s->regs[A_AP_OS_CLR / sizeof(uint32_t)] = val;
        break;
    case A_AP_RX_STAT:
    case A_AP_TX_STAT:
    case A_AP_OS_STAT:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "phytium-e2000-mhu: write to read-only register "
                      "at 0x%" HWADDR_PRIx "\n", offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "phytium-e2000-mhu: write to unimplemented register "
                      "at 0x%" HWADDR_PRIx "\n", offset);
        break;
    }
}

static const MemoryRegionOps phytium_e2000_mhu_ops = {
    .read = phytium_e2000_mhu_read,
    .write = phytium_e2000_mhu_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void phytium_e2000_mhu_reset(DeviceState *dev)
{
    PhytiumE2000MHUState *s = PHYTIUM_E2000_MHU(dev);

    memset(s->regs, 0, sizeof(s->regs));
    phytium_e2000_mhu_update_irq(s);
    phytium_e2000_scmi_writel(PHYTIUM_E2000_OS_SCMI_MBOX_BASE,
                              PHYTIUM_E2000_SCMI_STATUS_OFFSET,
                              PHYTIUM_E2000_SCMI_STATUS_FREE);
}

void phytium_e2000_mhu_connect_cpu(PhytiumE2000MHUState *s,
                                   unsigned int index, uint64_t mpidr,
                                   CPUState *cpu)
{
    g_assert(!DEVICE(s)->realized);
    g_assert(index < PHYTIUM_E2000_MHU_MAX_CPUS);
    g_assert(index == s->num_cpus);
    g_assert(cpu);

    object_ref(OBJECT(cpu));
    s->cpus[index] = cpu;
    s->cpu_mpidrs[index] = mpidr;
    s->num_cpus++;
}

void phytium_e2000_mhu_set_secondary_vector_slot(PhytiumE2000MHUState *s,
                                                 hwaddr slot)
{
    /*
     * This is immutable firmware configuration, not guest-programmable MHU
     * state.  Requiring it before realization prevents CPU_ON from observing
     * a partially configured transport.
     */
    g_assert(!DEVICE(s)->realized);
    g_assert(!s->secondary_vector_slot);
    g_assert(slot && QEMU_IS_ALIGNED(slot, sizeof(uint64_t)));

    s->secondary_vector_slot = slot;
}

static void phytium_e2000_mhu_init(Object *obj)
{
    PhytiumE2000MHUState *s = PHYTIUM_E2000_MHU(obj);

    memory_region_init_io(&s->iomem, obj, &phytium_e2000_mhu_ops, s,
                          TYPE_PHYTIUM_E2000_MHU,
                          PHYTIUM_E2000_MHU_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
}

static void phytium_e2000_mhu_finalize(Object *obj)
{
    PhytiumE2000MHUState *s = PHYTIUM_E2000_MHU(obj);
    unsigned int i;

    for (i = 0; i < s->num_cpus; i++) {
        object_unref(OBJECT(s->cpus[i]));
    }
}

static int phytium_e2000_mhu_post_load(void *opaque, int version_id)
{
    PhytiumE2000MHUState *s = opaque;

    phytium_e2000_mhu_update_irq(s);
    return 0;
}

static const VMStateDescription phytium_e2000_mhu_vmsd = {
    .name = TYPE_PHYTIUM_E2000_MHU,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = phytium_e2000_mhu_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, PhytiumE2000MHUState,
                             PHYTIUM_E2000_MHU_R_MAX),
        VMSTATE_END_OF_LIST()
    },
};

static void phytium_e2000_mhu_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &phytium_e2000_mhu_vmsd;
    device_class_set_legacy_reset(dc, phytium_e2000_mhu_reset);
}

static const TypeInfo phytium_e2000_mhu_info = {
    .name = TYPE_PHYTIUM_E2000_MHU,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PhytiumE2000MHUState),
    .instance_init = phytium_e2000_mhu_init,
    .instance_finalize = phytium_e2000_mhu_finalize,
    .class_init = phytium_e2000_mhu_class_init,
};

static void phytium_e2000_mhu_register_types(void)
{
    type_register_static(&phytium_e2000_mhu_info);
}

type_init(phytium_e2000_mhu_register_types)
