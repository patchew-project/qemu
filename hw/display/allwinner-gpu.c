/*
 * Allwinner GPU Module emulation
 *
 * Copyright (C) 2023 Strahinja Jankovic <strahinja.p.jankovic@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/display/allwinner-gpu.h"
#include "trace.h"

/* GPU register offsets - only the important ones. */
enum {
    REG_MALI_GP_CMD             = 0x0020,
    REG_MALI_GP_INT_RAWSTAT     = 0x0024,
    REG_MALI_GP_VERSION         = 0x006C,
    REG_MALI_GP_MMU_DTE         = 0x3000,
    REG_MALI_GP_MMU_STATUS      = 0x3004,
    REG_MALI_GP_MMU_COMMAND     = 0x3008,
    REG_MALI_PP0_MMU_DTE        = 0x4000,
    REG_MALI_PP0_MMU_STATUS     = 0x4004,
    REG_MALI_PP0_MMU_COMMAND    = 0x4008,
    REG_MALI_PP0_VERSION        = 0x9000,
    REG_MALI_PP0_CTRL           = 0x900C,
    REG_MALI_PP0_INT_RAWSTAT    = 0x9020,
};

#define REG_INDEX(offset)    (offset / sizeof(uint32_t))

#define MALI_GP_VERSION_READ_VAL    (0x0B07u << 16)
#define MALI_PP0_VERSION_READ_VAL   (0xCD07u << 16)
#define MALI_MMU_DTE_MASK           (0x0FFF)

/* MALI_GP_CMD register fields */
#define MALI_GP_CMD_SOFT_RESET    (1 << 10)

/* MALI_GP_INT_RAWSTAT register fields */
#define MALI_GP_INT_RAWSTAT_RESET_COMPLETED (1 << 19)

/* MALI_MMU_COMMAND values */
enum {
    MALI_MMU_COMMAND_ENABLE_PAGING = 0,
    MALI_MMU_COMMAND_HARD_RESET    = 6,
};

/* MALI_MMU_STATUS register fields */
#define MALI_MMU_STATUS_PAGING_ENABLED  (1 << 0)

/* MALI_PP_CTRL register fields */
#define MALI_PP_CTRL_SOFT_RESET     (1 << 7)

/* MALI_PP_INT_RAWSTAT register fields */
#define MALI_PP_INT_RAWSTAT_RESET_COMPLETED (1 << 12)

static uint64_t allwinner_gpu_read(void *opaque, hwaddr offset,
                                       unsigned size)
{
    const AwGpuState *s = AW_GPU(opaque);
    const uint32_t idx = REG_INDEX(offset);
    uint32_t val = s->regs[idx];

    switch (offset) {
    case REG_MALI_GP_VERSION:
        val = MALI_GP_VERSION_READ_VAL;
        break;
    case REG_MALI_GP_MMU_DTE:
    case REG_MALI_PP0_MMU_DTE:
        val &= ~MALI_MMU_DTE_MASK;
        break;
    case REG_MALI_PP0_VERSION:
        val = MALI_PP0_VERSION_READ_VAL;
        break;
    case 0xF0B8 ... AW_GPU_IOSIZE:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                  __func__, (uint32_t)offset);
        return 0;
    default:
        break;
    }

    trace_allwinner_gpu_read(offset, val);

    return val;
}

static void allwinner_gpu_write(void *opaque, hwaddr offset,
                                   uint64_t val, unsigned size)
{
    AwGpuState *s = AW_GPU(opaque);
    const uint32_t idx = REG_INDEX(offset);

    trace_allwinner_gpu_write(offset, (uint32_t)val);

    switch (offset) {
    case REG_MALI_GP_CMD:
        if (val == MALI_GP_CMD_SOFT_RESET) {
            s->regs[REG_INDEX(REG_MALI_GP_INT_RAWSTAT)] |=
                MALI_GP_INT_RAWSTAT_RESET_COMPLETED;
        }
        break;
    case REG_MALI_GP_MMU_COMMAND:
        if (val == MALI_MMU_COMMAND_ENABLE_PAGING) {
            s->regs[REG_INDEX(REG_MALI_GP_MMU_STATUS)] |=
                MALI_MMU_STATUS_PAGING_ENABLED;
        } else if (val == MALI_MMU_COMMAND_HARD_RESET) {
            s->regs[REG_INDEX(REG_MALI_GP_MMU_DTE)] = 0;
        }
        break;
    case REG_MALI_PP0_MMU_COMMAND:
        if (val == MALI_MMU_COMMAND_ENABLE_PAGING) {
            s->regs[REG_INDEX(REG_MALI_PP0_MMU_STATUS)] |=
                MALI_MMU_STATUS_PAGING_ENABLED;
        } else if (val == MALI_MMU_COMMAND_HARD_RESET) {
            s->regs[REG_INDEX(REG_MALI_PP0_MMU_DTE)] = 0;
        }
        break;
    case REG_MALI_PP0_CTRL:
        if (val == MALI_PP_CTRL_SOFT_RESET) {
            s->regs[REG_INDEX(REG_MALI_PP0_INT_RAWSTAT)] =
                MALI_PP_INT_RAWSTAT_RESET_COMPLETED;
        }
        break;
    case 0xF0B8 ... AW_GPU_IOSIZE:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                  __func__, (uint32_t)offset);
        break;
    default:
        break;
    }

    s->regs[idx] = (uint32_t) val;
}

static const MemoryRegionOps allwinner_gpu_ops = {
    .read = allwinner_gpu_read,
    .write = allwinner_gpu_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl.min_access_size = 4,
};

static void allwinner_gpu_reset_enter(Object *obj, ResetType type)
{
    AwGpuState *s = AW_GPU(obj);

    memset(&s->regs[0], 0, AW_GPU_IOSIZE);
}

static void allwinner_gpu_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    AwGpuState *s = AW_GPU(obj);

    /* Memory mapping */
    memory_region_init_io(&s->iomem, OBJECT(s), &allwinner_gpu_ops, s,
                          TYPE_AW_GPU, AW_GPU_IOSIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription allwinner_gpu_vmstate = {
    .name = "allwinner-gpu",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, AwGpuState, AW_GPU_REGS_NUM),
        VMSTATE_END_OF_LIST()
    }
};

static void allwinner_gpu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.enter = allwinner_gpu_reset_enter;
    dc->vmsd = &allwinner_gpu_vmstate;
}

static const TypeInfo allwinner_gpu_info = {
    .name          = TYPE_AW_GPU,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = allwinner_gpu_init,
    .instance_size = sizeof(AwGpuState),
    .class_init    = allwinner_gpu_class_init,
};

static void allwinner_gpu_register(void)
{
    type_register_static(&allwinner_gpu_info);
}

type_init(allwinner_gpu_register)
