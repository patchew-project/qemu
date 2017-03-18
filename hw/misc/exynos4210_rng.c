/*
 *  Exynos4210 Pseudo Random Nubmer Generator Emulation
 *
 *  Copyright (c) 2017 Krzysztof Kozlowski <krzk@kernel.org>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/log.h"

#define DEBUG_EXYNOS_RNG 0

#define DPRINTF(fmt, ...) \
    do { \
        if (DEBUG_EXYNOS_RNG) { \
            printf("exynos4210_rng: " fmt, ## __VA_ARGS__); \
        } \
    } while (0)

#define TYPE_EXYNOS4210_RNG             "exynos4210.rng"
#define EXYNOS4210_RNG(obj) \
    OBJECT_CHECK(Exynos4210RngState, (obj), TYPE_EXYNOS4210_RNG)

/*
 * Exynos4220, PRNG, only polling mode is supported.
 */

/* RNG_CONTROL_1 register bitfields, reset value: 0x0 */
#define EXYNOS4210_RNG_CONTROL_1_PRNG           0x8
#define EXYNOS4210_RNG_CONTROL_1_START_INIT     BIT(4)
/* RNG_STATUS register bitfields, reset value: 0x1 */
#define EXYNOS4210_RNG_STATUS_PRNG_ERROR        BIT(7)
#define EXYNOS4210_RNG_STATUS_PRNG_DONE         BIT(5)
#define EXYNOS4210_RNG_STATUS_MSG_DONE          BIT(4)
#define EXYNOS4210_RNG_STATUS_PARTIAL_DONE      BIT(3)
#define EXYNOS4210_RNG_STATUS_PRNG_BUSY         BIT(2)
#define EXYNOS4210_RNG_STATUS_SEED_SETTING_DONE BIT(1)
#define EXYNOS4210_RNG_STATUS_BUFFER_READY      BIT(0)
#define EXYNOS4210_RNG_STATUS_WRITE_MASK        (EXYNOS4210_RNG_STATUS_PRNG_DONE \
                                                    | EXYNOS4210_RNG_STATUS_MSG_DONE \
                                                    | EXYNOS4210_RNG_STATUS_PARTIAL_DONE)

#define EXYNOS4210_RNG_CONTROL_1                  0x0
#define EXYNOS4210_RNG_STATUS                    0x10
#define EXYNOS4210_RNG_SEED_IN                  0x140
#define EXYNOS4210_RNG_SEED_IN_OFFSET(n)        (EXYNOS4210_RNG_SEED_IN + (n * 0x4))
#define EXYNOS4210_RNG_PRNG                     0x160
#define EXYNOS4210_RNG_PRNG_OFFSET(n)           (EXYNOS4210_RNG_PRNG + (n * 0x4))

#define EXYNOS4210_RNG_PRNG_NUM                 5

#define EXYNOS4210_RNG_REGS_MEM_SIZE            0x200

typedef struct Exynos4210RngState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    GRand *grand[EXYNOS4210_RNG_PRNG_NUM];
    int32_t randr_value[EXYNOS4210_RNG_PRNG_NUM];
    uint32_t seeds[EXYNOS4210_RNG_PRNG_NUM];

    /* Register values */
    uint32_t reg_control;
    uint32_t reg_status;
} Exynos4210RngState;

static bool exynos4210_rng_seed_ready(const Exynos4210RngState *s)
{
    unsigned int i;

    for (i = 0; i < EXYNOS4210_RNG_PRNG_NUM; i++) {
        /*
         * Assuming 0 as invalid (uninitialized) seed value.
         * This also matches the reset value for SEED registers.
         */
        if (!s->seeds[i]) {
            return false;
        }
    }

    return true;
}

static void exynos4210_rng_set_seed(Exynos4210RngState *s, unsigned int i,
                                    uint64_t val)
{
    s->seeds[i] = val & UINT32_MAX;
    if (s->grand[i]) {
        g_rand_free(s->grand[i]);
    }
    s->grand[i] = g_rand_new_with_seed(s->seeds[i]);

    /* If all seeds were written, update the status to reflect it */
    if (exynos4210_rng_seed_ready(s)) {
        s->reg_status |= EXYNOS4210_RNG_STATUS_SEED_SETTING_DONE;
    } else {
        s->reg_status &= ~EXYNOS4210_RNG_STATUS_SEED_SETTING_DONE;
    }
}

static void exynos4210_rng_run_engine(Exynos4210RngState *s)
{
    unsigned int i;

    /* Seed set? */
    if ((s->reg_status & EXYNOS4210_RNG_STATUS_SEED_SETTING_DONE) == 0) {
        goto out;
    }

    /* PRNG engine chosen? */
    if ((s->reg_control & EXYNOS4210_RNG_CONTROL_1_PRNG) == 0) {
        goto out;
    }

    /* PRNG engine started? */
    if ((s->reg_control & EXYNOS4210_RNG_CONTROL_1_START_INIT) == 0) {
        goto out;
    }

    /* Get randoms */
    for (i = 0; i < EXYNOS4210_RNG_PRNG_NUM; i++) {
        s->randr_value[i] = g_rand_int(s->grand[i]);
    }

    /* Notify that PRNG is ready */
    s->reg_status |= EXYNOS4210_RNG_STATUS_PRNG_DONE;

out:
    /* Always clear start engine bit */
    s->reg_control &= ~EXYNOS4210_RNG_CONTROL_1_START_INIT;
}

static uint64_t exynos4210_rng_read(void *opaque, hwaddr offset,
                                    unsigned size)
{
    Exynos4210RngState *s = (Exynos4210RngState *)opaque;
    uint32_t val = 0;

    assert(size == 4);

    switch (offset) {
    case EXYNOS4210_RNG_CONTROL_1:
        val = s->reg_control;
        break;

    case EXYNOS4210_RNG_STATUS:
        val = s->reg_status;
        break;

    case EXYNOS4210_RNG_PRNG_OFFSET(0):
    case EXYNOS4210_RNG_PRNG_OFFSET(1):
    case EXYNOS4210_RNG_PRNG_OFFSET(2):
    case EXYNOS4210_RNG_PRNG_OFFSET(3):
    case EXYNOS4210_RNG_PRNG_OFFSET(4):
        val = s->randr_value[(offset - EXYNOS4210_RNG_PRNG_OFFSET(0)) / 4];
        DPRINTF("returning random @0x%" HWADDR_PRIx ": 0x%" PRIx32 "\n",
                offset, val);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad read offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
    }

    return val;
}

static void exynos4210_rng_write(void *opaque, hwaddr offset,
                                 uint64_t val, unsigned size)
{
    Exynos4210RngState *s = (Exynos4210RngState *)opaque;

    assert(size == 4);

    switch (offset) {
    case EXYNOS4210_RNG_CONTROL_1:
        DPRINTF("RNG_CONTROL_1 = 0x%" PRIx64 "\n", val);
        s->reg_control = val;
        exynos4210_rng_run_engine(s);
        break;

    case EXYNOS4210_RNG_STATUS:
        /* For clearing status fields */
        s->reg_status &= ~EXYNOS4210_RNG_STATUS_WRITE_MASK;
        s->reg_status |= val & EXYNOS4210_RNG_STATUS_WRITE_MASK;
        break;

    case EXYNOS4210_RNG_SEED_IN_OFFSET(0):
    case EXYNOS4210_RNG_SEED_IN_OFFSET(1):
    case EXYNOS4210_RNG_SEED_IN_OFFSET(2):
    case EXYNOS4210_RNG_SEED_IN_OFFSET(3):
    case EXYNOS4210_RNG_SEED_IN_OFFSET(4):
        exynos4210_rng_set_seed(s,
                                (offset - EXYNOS4210_RNG_SEED_IN_OFFSET(0)) / 4,
                                val);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad write offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
    }
}

static const MemoryRegionOps exynos4210_rng_ops = {
    .read = exynos4210_rng_read,
    .write = exynos4210_rng_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void exynos4210_rng_free_grand(Exynos4210RngState *s)
{
    unsigned int i;

    for (i = 0; i < EXYNOS4210_RNG_PRNG_NUM; i++) {
        if (s->grand[i]) {
            g_rand_free(s->grand[i]);
            s->grand[i] = NULL;
        }
    }
}

static void exynos4210_rng_reset(DeviceState *dev)
{
    Exynos4210RngState *s = EXYNOS4210_RNG(dev);

    s->reg_control = 0;
    s->reg_status = EXYNOS4210_RNG_STATUS_BUFFER_READY;
    memset(s->seeds, 0, EXYNOS4210_RNG_PRNG_NUM * sizeof(*(s->seeds)));
    memset(s->seeds, 0, EXYNOS4210_RNG_PRNG_NUM * sizeof(*(s->randr_value)));

    exynos4210_rng_free_grand(s);
}

static void exynos4210_rng_init(Object *obj)
{
    Exynos4210RngState *s = EXYNOS4210_RNG(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &exynos4210_rng_ops, s,
                          TYPE_EXYNOS4210_RNG, EXYNOS4210_RNG_REGS_MEM_SIZE);
    sysbus_init_mmio(dev, &s->iomem);
}

static void exynos4210_rng_finalize(Object *obj)
{
    Exynos4210RngState *s = EXYNOS4210_RNG(obj);

    exynos4210_rng_free_grand(s);
}

static const VMStateDescription exynos4210_rng_vmstate = {
    .name = TYPE_EXYNOS4210_RNG,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_INT32_ARRAY(randr_value, Exynos4210RngState,
                            EXYNOS4210_RNG_PRNG_NUM),
        VMSTATE_UINT32_ARRAY(seeds, Exynos4210RngState,
                             EXYNOS4210_RNG_PRNG_NUM),
        VMSTATE_UINT32(reg_status, Exynos4210RngState),
        VMSTATE_UINT32(reg_control, Exynos4210RngState),
        VMSTATE_END_OF_LIST()
    }
};

static void exynos4210_rng_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = exynos4210_rng_reset;
    dc->vmsd = &exynos4210_rng_vmstate;
}

static const TypeInfo exynos4210_rng_info = {
    .name          = TYPE_EXYNOS4210_RNG,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Exynos4210RngState),
    .instance_init = exynos4210_rng_init,
    .instance_finalize  = exynos4210_rng_finalize,
    .class_init    = exynos4210_rng_class_init,
};

static void exynos4210_rng_register(void)
{
    type_register_static(&exynos4210_rng_info);
}

type_init(exynos4210_rng_register)
