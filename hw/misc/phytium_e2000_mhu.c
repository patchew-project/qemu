/*
 * Phytium E2000 MHU/SCMI doorbell
 *
 * This is a boot-oriented SCMI transport proxy. It acknowledges requests in
 * shared SRAM so PBF can complete clock and platform setup; it is not a full
 * SCMI protocol server.
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

#include "hw/core/register.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/module.h"
#include "system/address-spaces.h"
#include "target/arm/arm-powerctl.h"
#include "target/arm/cpu.h"

#define PHYTIUM_E2000_PBF_SCMI_MBOX_BASE  0x32a10400
#define PHYTIUM_E2000_SCMI_STATUS_OFFSET  0x04
#define PHYTIUM_E2000_SCMI_LEN_OFFSET     0x14
#define PHYTIUM_E2000_SCMI_HEADER_OFFSET  0x18
#define PHYTIUM_E2000_SCMI_PAYLOAD_OFFSET 0x1c
#define PHYTIUM_E2000_SCMI_STATUS_FREE    BIT(0)

#define SCMI_MESSAGE_ID(header)    extract32((header), 0, 8)
#define SCMI_PROTOCOL_ID(header)   extract32((header), 10, 8)
#define SCMI_PROTOCOL_POWER_DOMAIN 0x11
#define SCMI_PROTOCOL_PHYTIUM      0x81
#define SCMI_POWER_STATE_SET       0x4
#define SCMI_PHYTIUM_GET_PSOSTAT   0x3
#define SCMI_POWER_STATE_TYPE      BIT(30)
#define SCMI_POWER_STATE_ID_MASK   (SCMI_POWER_STATE_TYPE - 1)

#define SCMI_SUCCESS            0
#define SCMI_INVALID_PARAMETERS (-2)
#define SCMI_GENERIC_ERROR      (-8)

#define PHYTIUM_E2000_PBF_ROOT_ANCHOR       \
    (PHYTIUM_E2000_PBR_BOOT_SRAM_BASE + 0xf00)
#define PHYTIUM_E2000_CPU_TARGET_OFFSET     0x08
#define PHYTIUM_E2000_CPU_LOCK_DEPTH_OFFSET 0x28
#define PHYTIUM_E2000_CPU_LOCK_OWNER_OFFSET 0x30
#define PHYTIUM_E2000_CPU_ON_COMPLETE       0xabcdef98

/*
 * The SDK defines AP OS status/set/clear at 0x100/0x108/0x110 within a
 * channel. PBF selects the channel at MHU offset 0x200, producing the global
 * offsets below. Writes to AP_OS_SET are the request notification.
 */
REG32(AP_OS_STAT, 0x300)
REG32(AP_OS_SET, 0x308)
REG32(AP_OS_CLR, 0x310)

#define PHYTIUM_E2000_MHU_R_MAX \
    (PHYTIUM_E2000_MHU_MMIO_SIZE / sizeof(uint32_t))

struct PhytiumE2000MHUState {
    SysBusDevice parent_obj;

    uint32_t regs[PHYTIUM_E2000_MHU_R_MAX];
    RegisterInfo regs_info[PHYTIUM_E2000_MHU_R_MAX];
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

static uint32_t phytium_e2000_scmi_readl(hwaddr offset)
{
    uint8_t buf[sizeof(uint32_t)];

    address_space_read(&address_space_memory,
                       PHYTIUM_E2000_PBF_SCMI_MBOX_BASE + offset,
                       MEMTXATTRS_UNSPECIFIED, buf, sizeof(buf));
    return ldl_le_p(buf);
}

static void phytium_e2000_scmi_writel(hwaddr offset, uint32_t value)
{
    uint8_t buf[sizeof(uint32_t)];

    stl_le_p(buf, value);
    address_space_write(&address_space_memory,
                        PHYTIUM_E2000_PBF_SCMI_MBOX_BASE + offset,
                        MEMTXATTRS_UNSPECIFIED, buf, sizeof(buf));
}

static void phytium_e2000_scmi_publish(uint32_t len)
{
    phytium_e2000_scmi_writel(PHYTIUM_E2000_SCMI_LEN_OFFSET, len);
    /*
     * Publish the free bit last. PBF polls this field as the ownership handoff
     * and may consume the response immediately after observing it.
     */
    phytium_e2000_scmi_writel(PHYTIUM_E2000_SCMI_STATUS_OFFSET,
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
        PHYTIUM_E2000_SCMI_PAYLOAD_OFFSET + 4);
    uint32_t power_state = phytium_e2000_scmi_readl(
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
        PHYTIUM_E2000_SCMI_HEADER_OFFSET);
    uint32_t len = MAX(phytium_e2000_scmi_readl(
        PHYTIUM_E2000_SCMI_LEN_OFFSET), (uint32_t)sizeof(uint32_t));
    int32_t scmi_status = SCMI_SUCCESS;

    /*
     * PBF issues clock and platform setup commands whose side effects do not
     * affect modeled devices. Preserve their payload length and acknowledge
     * them; only messages that change modeled CPU state need special handling.
     */
    if (SCMI_PROTOCOL_ID(header) == SCMI_PROTOCOL_PHYTIUM &&
        SCMI_MESSAGE_ID(header) == SCMI_PHYTIUM_GET_PSOSTAT) {
        phytium_e2000_scmi_writel(PHYTIUM_E2000_SCMI_PAYLOAD_OFFSET + 4,
                                  phytium_e2000_mhu_psostat(s));
        len = 3 * sizeof(uint32_t);
    } else if (SCMI_PROTOCOL_ID(header) == SCMI_PROTOCOL_POWER_DOMAIN &&
               SCMI_MESSAGE_ID(header) == SCMI_POWER_STATE_SET) {
        scmi_status = phytium_e2000_mhu_set_power_state(s);
        len = 2 * sizeof(uint32_t);
    }

    phytium_e2000_scmi_writel(PHYTIUM_E2000_SCMI_PAYLOAD_OFFSET,
                              scmi_status);
    phytium_e2000_scmi_publish(len);
}

void phytium_e2000_mhu_seed_mailbox(void)
{
    /*
     * PBR leaves the shared channel available before releasing PBF. Seed the
     * same ownership and success state even before the first doorbell write.
     */
    phytium_e2000_scmi_writel(PHYTIUM_E2000_SCMI_PAYLOAD_OFFSET,
                              SCMI_SUCCESS);
    phytium_e2000_scmi_publish(sizeof(uint32_t));
}

static void phytium_e2000_mhu_doorbell_post_write(RegisterInfo *reg,
                                                  uint64_t value)
{
    PhytiumE2000MHUState *s = PHYTIUM_E2000_MHU(reg->opaque);

    /*
     * Complete requests synchronously because no separate SCP CPU executes in
     * this model. Zero writes only update doorbell storage.
     */
    if (value) {
        phytium_e2000_mhu_complete_scmi(s);
    }
}

static const RegisterAccessInfo phytium_e2000_mhu_regs_info[] = {
    /*
     * The functional transport does not model an SCP interrupt line. STAT is
     * therefore idle, SET completes the shared-memory transaction, and CLR
     * remains ordinary register storage for the firmware acknowledge path.
     */
    { .name = "AP_OS_STAT", .addr = A_AP_OS_STAT,
      .ro = UINT32_MAX },
    { .name = "AP_OS_SET", .addr = A_AP_OS_SET,
      .post_write = phytium_e2000_mhu_doorbell_post_write },
    { .name = "AP_OS_CLR", .addr = A_AP_OS_CLR },
};

static const MemoryRegionOps phytium_e2000_mhu_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
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
    int i;

    for (i = 0; i < ARRAY_SIZE(phytium_e2000_mhu_regs_info); i++) {
        register_reset(&s->regs_info[
            phytium_e2000_mhu_regs_info[i].addr / sizeof(uint32_t)]);
    }
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
    RegisterInfoArray *reg_array;

    reg_array = register_init_block32(
        DEVICE(obj), phytium_e2000_mhu_regs_info,
        ARRAY_SIZE(phytium_e2000_mhu_regs_info), s->regs_info, s->regs,
        &phytium_e2000_mhu_ops, false, PHYTIUM_E2000_MHU_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &reg_array->mem);
}

static void phytium_e2000_mhu_finalize(Object *obj)
{
    PhytiumE2000MHUState *s = PHYTIUM_E2000_MHU(obj);
    unsigned int i;

    for (i = 0; i < s->num_cpus; i++) {
        object_unref(OBJECT(s->cpus[i]));
    }
}

static const VMStateDescription phytium_e2000_mhu_vmsd = {
    .name = TYPE_PHYTIUM_E2000_MHU,
    .version_id = 1,
    .minimum_version_id = 1,
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
