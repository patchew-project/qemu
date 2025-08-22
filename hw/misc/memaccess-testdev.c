/*
 * QEMU memory access test device
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Author: Tomoyuki HIROSE <hrstmyk811m@gmail.com>
 *
 * This device is used to test memory acccess, like:
 * qemu-system-x86_64 -device memaccess-testdev,address=0x10000000
 */

#include "qemu/osdep.h"
#include "system/address-spaces.h"
#include "system/memory.h"
#include "hw/qdev-core.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qemu/typedefs.h"
#include "qom/object.h"
#include "hw/misc/memaccess-testdev.h"

typedef bool (*skip_func_ptr)(uint32_t valid_max, uint32_t valid_min,
                              bool valid_unaligned, uint32_t impl_max,
                              uint32_t impl_min, bool impl_unaligned);

typedef struct MrOpsList {
    const char *name;
    MemoryRegionOps *ops_array;
    const size_t ops_array_len;
    const size_t offset_idx;
    skip_func_ptr skip_fn;
    bool is_little;
} MrOpsList;

MemoryRegionOps ops_list_little_b_valid[N_OPS_LIST_LITTLE_B_VALID];
MemoryRegionOps ops_list_little_b_invalid[N_OPS_LIST_LITTLE_B_INVALID];
MemoryRegionOps ops_list_little_w_valid[N_OPS_LIST_LITTLE_W_VALID];
MemoryRegionOps ops_list_little_w_invalid[N_OPS_LIST_LITTLE_W_INVALID];
MemoryRegionOps ops_list_little_l_valid[N_OPS_LIST_LITTLE_L_VALID];
MemoryRegionOps ops_list_little_l_invalid[N_OPS_LIST_LITTLE_L_INVALID];
MemoryRegionOps ops_list_little_q_valid[N_OPS_LIST_LITTLE_Q_VALID];
MemoryRegionOps ops_list_little_q_invalid[N_OPS_LIST_LITTLE_Q_INVALID];
MemoryRegionOps ops_list_big_b_valid[N_OPS_LIST_BIG_B_VALID];
MemoryRegionOps ops_list_big_b_invalid[N_OPS_LIST_BIG_B_INVALID];
MemoryRegionOps ops_list_big_w_valid[N_OPS_LIST_BIG_W_VALID];
MemoryRegionOps ops_list_big_w_invalid[N_OPS_LIST_BIG_W_INVALID];
MemoryRegionOps ops_list_big_l_valid[N_OPS_LIST_BIG_L_VALID];
MemoryRegionOps ops_list_big_l_invalid[N_OPS_LIST_BIG_L_INVALID];
MemoryRegionOps ops_list_big_q_valid[N_OPS_LIST_BIG_Q_VALID];
MemoryRegionOps ops_list_big_q_invalid[N_OPS_LIST_BIG_Q_INVALID];

static bool skip_core(uint32_t required_min, bool valid_test,
                      uint32_t valid_max, uint32_t valid_min,
                      bool valid_unaligned, uint32_t impl_max,
                      uint32_t impl_min, bool impl_unaligned)
{
    if (valid_min != required_min) {
        return true;
    }
    if (valid_test) {
        if (!valid_unaligned) {
            return true;
        }
    } else {
        if (valid_unaligned || impl_unaligned) {
            return true;
        }
    }
    if (valid_max < valid_min) {
        return true;
    }

    if (impl_max < impl_min) {
        return true;
    }

    return false;
}

#define DEFINE_SKIP_VALID_INVALID_FN(NAME, REQ_MIN)                      \
    static bool skip_##NAME##_valid(uint32_t vm, uint32_t vn, bool vu,   \
                                    uint32_t im, uint32_t in, bool iu)   \
    {                                                                    \
        return skip_core(REQ_MIN, true, vm, vn, vu, im, in, iu);         \
    }                                                                    \
                                                                         \
    static bool skip_##NAME##_invalid(uint32_t vm, uint32_t vn, bool vu, \
                                      uint32_t im, uint32_t in, bool iu) \
    {                                                                    \
        return skip_core(REQ_MIN, false, vm, vn, vu, im, in, iu);        \
    }

DEFINE_SKIP_VALID_INVALID_FN(b, 1)
DEFINE_SKIP_VALID_INVALID_FN(w, 2)
DEFINE_SKIP_VALID_INVALID_FN(l, 4)
DEFINE_SKIP_VALID_INVALID_FN(q, 8)

static void testdev_init_memory_region(MemoryRegion *mr,
                                       Object *owner,
                                       const MemoryRegionOps *ops,
                                       void *opaque,
                                       const char *name,
                                       uint64_t size,
                                       MemoryRegion *container,
                                       hwaddr container_offset)
{
    memory_region_init_io(mr, owner, ops, opaque, name, size);
    memory_region_add_subregion(container, container_offset, mr);
}

static void testdev_init_from_mr_ops_list(MemAccessTestDev *testdev,
                                          const MrOpsList *l)
{
    for (size_t i = 0; i < l->ops_array_len; i++) {
        g_autofree gchar *name = g_strdup_printf("%s-%ld", l->name, i);
        testdev_init_memory_region(&testdev->memory_regions[l->offset_idx + i],
                                   OBJECT(testdev), &l->ops_array[i],
                                   testdev->mr_data[l->offset_idx + i],
                                   name,
                                   MEMACCESS_TESTDEV_REGION_SIZE,
                                   &testdev->container,
                                   MEMACCESS_TESTDEV_REGION_SIZE *
                                   (l->offset_idx + i));
    }
}

#define LITTLE 1
#define BIG    0
#define _DEFINE_MR_OPS_LIST(_n, _ops, _len, _off, _skipfn, _is_little) \
{                                                                      \
    .name          = (_n),                                             \
    .ops_array     = (_ops),                                           \
    .ops_array_len = (_len),                                           \
    .offset_idx    = (_off),                                           \
    .skip_fn       = (_skipfn),                                        \
    .is_little     = (_is_little),                                     \
}

#define DEFINE_MR_OPS_LIST(e, E, w, W, v, V)                    \
    _DEFINE_MR_OPS_LIST(                                        \
        #e "-" #w "-" #v,                /* .name            */ \
        ops_list_##e##_##w##_##v,        /* .ops_array       */ \
        N_OPS_LIST_##E##_##W##_##V,      /* .ops_array_len   */ \
        OFF_IDX_OPS_LIST_##E##_##W##_##V,/* .offset_idx      */ \
        skip_##w##_##v,                  /* .skip_fn         */ \
        E /* .is_little => 1 = little endian, 0 = big endian */ \
    )

static MrOpsList mr_ops_list[] = {
    DEFINE_MR_OPS_LIST(little, LITTLE, b, B, valid,   VALID),
    DEFINE_MR_OPS_LIST(little, LITTLE, b, B, invalid, INVALID),
    DEFINE_MR_OPS_LIST(little, LITTLE, w, W, valid,   VALID),
    DEFINE_MR_OPS_LIST(little, LITTLE, w, W, invalid, INVALID),
    DEFINE_MR_OPS_LIST(little, LITTLE, l, L, valid,   VALID),
    DEFINE_MR_OPS_LIST(little, LITTLE, l, L, invalid, INVALID),
    DEFINE_MR_OPS_LIST(little, LITTLE, q, Q, valid,   VALID),
    DEFINE_MR_OPS_LIST(little, LITTLE, q, Q, invalid, INVALID),
    DEFINE_MR_OPS_LIST(big,    BIG,    b, B, valid,   VALID),
    DEFINE_MR_OPS_LIST(big,    BIG,    b, B, invalid, INVALID),
    DEFINE_MR_OPS_LIST(big,    BIG,    w, W, valid,   VALID),
    DEFINE_MR_OPS_LIST(big,    BIG,    w, W, invalid, INVALID),
    DEFINE_MR_OPS_LIST(big,    BIG,    l, L, valid,   VALID),
    DEFINE_MR_OPS_LIST(big,    BIG,    l, L, invalid, INVALID),
    DEFINE_MR_OPS_LIST(big,    BIG,    q, Q, valid,   VALID),
    DEFINE_MR_OPS_LIST(big,    BIG,    q, Q, invalid, INVALID),
};
#undef LITTLE
#undef BIG

static uint64_t memaccess_testdev_read_little(void *opaque, hwaddr addr,
                                              unsigned int size)
{
    g_assert(addr + size < MEMACCESS_TESTDEV_REGION_SIZE);
    void *s = (uint8_t *)opaque + addr;
    return ldn_le_p(s, size);
}

static void memaccess_testdev_write_little(void *opaque, hwaddr addr,
                                           uint64_t data, unsigned int size)
{
    g_assert(addr + size < MEMACCESS_TESTDEV_REGION_SIZE);
    void *d = (uint8_t *)opaque + addr;
    stn_le_p(d, size, data);
}

static uint64_t memaccess_testdev_read_big(void *opaque, hwaddr addr,
                                           unsigned int size)
{
    g_assert(addr + size < MEMACCESS_TESTDEV_REGION_SIZE);
    void *s = (uint8_t *)opaque + addr;
    return ldn_be_p(s, size);
}

static void memaccess_testdev_write_big(void *opaque, hwaddr addr,
                                        uint64_t data, unsigned int size)
{
    g_assert(addr + size < MEMACCESS_TESTDEV_REGION_SIZE);
    void *d = (uint8_t *)opaque + addr;
    stn_be_p(d, size, data);
}

static void fill_ops_list(MemoryRegionOps *ops,
                          skip_func_ptr fptr,
                          size_t ops_len,
                          bool is_little)
{
    static const uint32_t sizes[] = { 1, 2, 4, 8 };
    static const bool bools[] = { false, true };
    int idx = 0;

    for (int vMaxIdx = 0; vMaxIdx < 4; vMaxIdx++) {
        for (int vMinIdx = 0; vMinIdx < 4; vMinIdx++) {
            for (int vUIdx = 0; vUIdx < 2; vUIdx++) {
                for (int iMaxIdx = 0; iMaxIdx < 4; iMaxIdx++) {
                    for (int iMinIdx = 0; iMinIdx < 4; iMinIdx++) {
                        for (int iUIdx = 0; iUIdx < 2; iUIdx++) {
                            uint32_t valid_max       = sizes[vMaxIdx];
                            uint32_t valid_min       = sizes[vMinIdx];
                            bool     valid_unaligned = bools[vUIdx];
                            uint32_t impl_max        = sizes[iMaxIdx];
                            uint32_t impl_min        = sizes[iMinIdx];
                            bool     impl_unaligned  = bools[iUIdx];

                            if (!fptr(valid_max, valid_min, valid_unaligned,
                                      impl_max, impl_min, impl_unaligned))
                            {
                                const MemoryRegionOps new_op = {
                                    .read = is_little ?
                                            memaccess_testdev_read_little :
                                            memaccess_testdev_read_big,
                                    .write = is_little ?
                                             memaccess_testdev_write_little :
                                             memaccess_testdev_write_big,
                                    .endianness = is_little ?
                                                  DEVICE_LITTLE_ENDIAN :
                                                  DEVICE_BIG_ENDIAN,
                                    .valid = {
                                        .max_access_size = valid_max,
                                        .min_access_size = valid_min,
                                        .unaligned      = valid_unaligned,
                                    },
                                    .impl = {
                                        .max_access_size = impl_max,
                                        .min_access_size = impl_min,
                                        .unaligned       = impl_unaligned,
                                    },
                                };

                                ops[idx] = new_op;
                                idx++;
                                if (idx > ops_len) {
                                    g_assert_not_reached();
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

#define N_MR_OPS_LIST (sizeof(mr_ops_list) / sizeof(MrOpsList))

static void init_testdev(MemAccessTestDev *testdev)
{
    memory_region_init(&testdev->container, OBJECT(testdev), "memtest-regions",
                       MEMACCESS_TESTDEV_REGION_SIZE * N_OPS_LIST);
    testdev->mr_data = g_malloc(MEMACCESS_TESTDEV_MR_DATA_SIZE);

    for (size_t i = 0; i < N_MR_OPS_LIST; i++) {
        fill_ops_list(
            mr_ops_list[i].ops_array,
            mr_ops_list[i].skip_fn,
            mr_ops_list[i].ops_array_len,
            mr_ops_list[i].is_little
        );
        testdev_init_from_mr_ops_list(testdev, &mr_ops_list[i]);
    }

    memory_region_add_subregion(get_system_memory(), testdev->base,
                                &testdev->container);
}

static void memaccess_testdev_realize(DeviceState *dev, Error **errp)
{
    MemAccessTestDev *d = MEM_ACCESS_TEST_DEV(dev);

    if (d->base == UINT64_MAX) {
        error_setg(errp, "base address is not assigned");
        return;
    }

    init_testdev(d);
}

static void memaccess_testdev_unrealize(DeviceState *dev)
{
    MemAccessTestDev *d = MEM_ACCESS_TEST_DEV(dev);
    g_free(d->mr_data);
}

static Property memaccess_testdev_props[] = {
    DEFINE_PROP_UINT64("address", MemAccessTestDev, base, UINT64_MAX),
};

static void memaccess_testdev_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = memaccess_testdev_realize;
    dc->unrealize = memaccess_testdev_unrealize;
    device_class_set_props_n(dc,
                             memaccess_testdev_props,
                             ARRAY_SIZE(memaccess_testdev_props));
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo memaccess_testdev_info = {
    .name = TYPE_MEM_ACCESS_TEST_DEV,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(MemAccessTestDev),
    .class_init = memaccess_testdev_class_init,
};

static void memaccess_testdev_register_types(void)
{
    type_register_static(&memaccess_testdev_info);
}

type_init(memaccess_testdev_register_types)
