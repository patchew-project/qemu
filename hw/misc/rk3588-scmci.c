/*
 * Rockchip RK3588 SCMI server
 *
 * The RK3588 firmware exposes SCMI through the SMC transport rather
 * than through a mailbox.  This model provides the shared memory area
 * and a minimal server implementing the Base and Clock protocols. Clock rates
 * and enable state are maintained independently of CRU registers. It is
 * intended to let a guest exercise the standard arm,scmi-smc path without
 * running a complete BL31 image.
 *
 * Copyright (C) 2026 Process Mission
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/bswap.h"
#include "hw/arm/arm-smc.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/resettable.h"
#include "hw/core/sysbus.h"
#include "hw/misc/rk3588-scmci.h"
#include "system/memory.h"

#define RK3588_SCMI_SHORT_NAME_SIZE     16
#define RK3588_SCMI_MAX_STR_SIZE        64

/*
 * Use the U-Boot v2025.01 SPL boot rate, with DSU using its default
 * LPLL source and divide-by-one setting.
 */
#define RK3588_SCMI_BOOT_RATE           816000000ULL

#define RK3588_SCMI_CPUL                0
#define RK3588_SCMI_DSU                 1
#define RK3588_SCMI_CPUB01              2
#define RK3588_SCMI_CPUB23              3

/*
 * TF-A v2.12 PVTPLL nominal rates, sorted ascending for enumeration and
 * selection. These differ from the firmware's advertised rate lists.
 */
static const uint64_t rk3588_cpul_rates[] = {
    408000000, 600000000, 816000000, 1008000000, 1200000000,
    1416000000, 1608000000, 1704000000, 1800000000,
};

static const uint64_t rk3588_cpub_rates[] = {
    408000000, 600000000, 816000000, 1008000000, 1200000000,
    1416000000, 1608000000, 1800000000, 2016000000, 2112000000,
    2208000000, 2256000000, 2304000000, 2352000000, 2400000000,
};

static const struct {
    const char *name;
    const uint64_t *rates;
    size_t num_rates;
} rk3588_scmi_clocks[] = {
    [RK3588_SCMI_CPUL] = {
        "scmi_clk_cpul", rk3588_cpul_rates, ARRAY_SIZE(rk3588_cpul_rates),
    },
    [RK3588_SCMI_DSU] = {
        "scmi_clk_dsu", rk3588_cpul_rates, ARRAY_SIZE(rk3588_cpul_rates),
    },
    [RK3588_SCMI_CPUB01] = {
        "scmi_clk_cpub01", rk3588_cpub_rates, ARRAY_SIZE(rk3588_cpub_rates),
    },
    [RK3588_SCMI_CPUB23] = {
        "scmi_clk_cpub23", rk3588_cpub_rates, ARRAY_SIZE(rk3588_cpub_rates),
    },
};

/* SCMI shared memory layout, see Linux struct scmi_shared_mem. */
#define SCMI_SHMEM_RESERVED             0x00
#define SCMI_SHMEM_CHANNEL_STATUS       0x04
#define SCMI_SHMEM_RESERVED1            0x08
#define SCMI_SHMEM_FLAGS                0x10
#define SCMI_SHMEM_LENGTH               0x14
#define SCMI_SHMEM_MSG_HEADER           0x18
#define SCMI_SHMEM_MSG_PAYLOAD          0x1c

/* Space for response data after the 32-bit SCMI status code. */
#define RK3588_SCMI_RESP_MAX \
    (RK3588_SCMI_SHMEM_SIZE - SCMI_SHMEM_MSG_PAYLOAD - sizeof(uint32_t))

#define SCMI_SHMEM_CHANNEL_FREE         BIT(0)
#define SCMI_SHMEM_CHANNEL_ERROR        BIT(1)

#define SCMI_MSG_ID(hdr)                ((hdr) & 0xff)
#define SCMI_MSG_PROTOCOL(hdr)          (((hdr) >> 10) & 0xff)

#define SCMI_PROTOCOL_BASE              0x10
#define SCMI_PROTOCOL_CLOCK             0x14

#define SCMI_SUCCESS                    0
#define SCMI_ERR_SUPPORT                (-1)
#define SCMI_ERR_PARAMS                 (-2)
#define SCMI_ERR_NOT_FOUND              (-4)
#define SCMI_ERR_OUT_OF_RANGE           (-5)
#define SCMI_ERR_PROTOCOL               (-10)

#define SCMI_BASE_PROTOCOL_VERSION      0x00020001
#define SCMI_CLOCK_PROTOCOL_VERSION     0x00020000

#define SCMI_CLOCK_EXTENDED_NAMES        BIT(29)
#define SCMI_CLOCK_ENABLED               BIT(0)

#define SCMI_CLOCK_SET_ASYNC            BIT(0)
#define SCMI_CLOCK_SET_IGNORE_RESP      BIT(1)
#define SCMI_CLOCK_SET_ROUND_UP         BIT(2)
#define SCMI_CLOCK_SET_ROUND_AUTO       BIT(3)

/* Each rate list is preceded by a 32-bit count/flags field. */
G_STATIC_ASSERT(sizeof(rk3588_cpul_rates) + sizeof(uint32_t) <=
                RK3588_SCMI_RESP_MAX);
G_STATIC_ASSERT(sizeof(rk3588_cpub_rates) + sizeof(uint32_t) <=
                RK3588_SCMI_RESP_MAX);

struct RK3588SCMIState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion shmem;
    uint8_t *shmem_ptr;
    uint64_t clock_rate[ARRAY_SIZE(rk3588_scmi_clocks)];
    uint32_t clock_config[ARRAY_SIZE(rk3588_scmi_clocks)];
    bool smc_registered;
};

static uint32_t rk3588_scmi_rd32(RK3588SCMIState *s, hwaddr offset)
{
    return ldl_le_p(s->shmem_ptr + offset);
}

static void rk3588_scmi_wr32(RK3588SCMIState *s, hwaddr offset, uint32_t value)
{
    stl_le_p(s->shmem_ptr + offset, value);
}

static void rk3588_scmi_put_name(uint8_t *dst, const char *name)
{
    size_t len = strlen(name);

    memset(dst, 0, RK3588_SCMI_SHORT_NAME_SIZE);
    memcpy(dst, name, MIN(len, RK3588_SCMI_SHORT_NAME_SIZE));
}

static int32_t rk3588_scmi_base_protocol(RK3588SCMIState *s, uint8_t msg_id,
                                         const uint8_t *req, size_t req_len,
                                         uint8_t *resp, size_t *resp_len)
{
    uint32_t value;

    switch (msg_id) {
    case 0x0: /* PROTOCOL_VERSION */
        stl_le_p(resp, SCMI_BASE_PROTOCOL_VERSION);
        *resp_len = 4;
        return SCMI_SUCCESS;

    case 0x1: /* PROTOCOL_ATTRIBUTES */
        resp[0] = 1; /* one implemented protocol: Clock */
        resp[1] = 1; /* one agent */
        resp[2] = 0;
        resp[3] = 0;
        *resp_len = 4;
        return SCMI_SUCCESS;

    case 0x2: /* PROTOCOL_MESSAGE_ATTRIBUTES */
        if (req_len < 4) {
            return SCMI_ERR_PROTOCOL;
        }
        value = ldl_le_p(req);
        if (value > 0x7) {
            return SCMI_ERR_NOT_FOUND;
        }
        stl_le_p(resp, 0);
        *resp_len = 4;
        return SCMI_SUCCESS;

    case 0x3: /* BASE_DISCOVER_VENDOR */
    case 0x4: /* BASE_DISCOVER_SUB_VENDOR */
        rk3588_scmi_put_name(resp, "QEMU");
        *resp_len = RK3588_SCMI_SHORT_NAME_SIZE;
        return SCMI_SUCCESS;

    case 0x5: /* BASE_DISCOVER_IMPLEMENT_VERSION */
        stl_le_p(resp, 1);
        *resp_len = 4;
        return SCMI_SUCCESS;

    case 0x6: /* BASE_DISCOVER_LIST_PROTOCOLS */
        if (req_len < 4) {
            return SCMI_ERR_PROTOCOL;
        }
        value = ldl_le_p(req);
        if (value == 0) {
            stl_le_p(resp, 1);
            resp[4] = SCMI_PROTOCOL_CLOCK;
            resp[5] = 0;
            resp[6] = 0;
            resp[7] = 0;
            *resp_len = 8;
        } else {
            stl_le_p(resp, 0);
            *resp_len = 4;
        }
        return SCMI_SUCCESS;

    case 0x7: /* BASE_DISCOVER_AGENT */
        if (req_len < 4) {
            return SCMI_ERR_PROTOCOL;
        }
        value = ldl_le_p(req);
        if (value == UINT32_MAX) {
            value = 1; /* The single agent using this channel. */
        }
        if (value > 1) {
            return SCMI_ERR_NOT_FOUND;
        }
        stl_le_p(resp, value);
        rk3588_scmi_put_name(resp + 4, value ? "QEMU" : "platform");
        *resp_len = 4 + RK3588_SCMI_SHORT_NAME_SIZE;
        return SCMI_SUCCESS;

    default:
        return SCMI_ERR_SUPPORT;
    }
}

/*
 * Caller validates id. ROUND_AUTO takes precedence; ties select the lower rate.
 */
static int32_t rk3588_scmi_select_rate(uint32_t id, uint32_t flags,
                                       uint64_t requested, uint64_t *selected)
{
    const uint64_t *rates = rk3588_scmi_clocks[id].rates;
    size_t count = rk3588_scmi_clocks[id].num_rates;
    size_t i;

    for (i = 0; i < count && rates[i] < requested; i++) {
        /* Find the first rate at least as high as requested. */
    }
    if (i < count && rates[i] == requested) {
        *selected = rates[i];
    } else if (flags & SCMI_CLOCK_SET_ROUND_AUTO) {
        if (i == 0) {
            *selected = rates[0];
        } else if (i == count ||
                   requested - rates[i - 1] <= rates[i] - requested) {
            *selected = rates[i - 1];
        } else {
            *selected = rates[i];
        }
    } else if (flags & SCMI_CLOCK_SET_ROUND_UP) {
        if (i == count) {
            return SCMI_ERR_OUT_OF_RANGE;
        }
        *selected = rates[i];
    } else {
        if (i == 0) {
            return SCMI_ERR_OUT_OF_RANGE;
        }
        *selected = rates[i - 1];
    }
    return SCMI_SUCCESS;
}

static int32_t rk3588_scmi_clock_protocol(RK3588SCMIState *s, uint8_t msg_id,
                                          const uint8_t *req, size_t req_len,
                                          uint8_t *resp, size_t *resp_len)
{
    uint32_t id;

    switch (msg_id) {
    case 0x0: /* PROTOCOL_VERSION */
        stl_le_p(resp, SCMI_CLOCK_PROTOCOL_VERSION);
        *resp_len = 4;
        return SCMI_SUCCESS;

    case 0x1: /* PROTOCOL_ATTRIBUTES */
        stw_le_p(resp, ARRAY_SIZE(rk3588_scmi_clocks));
        resp[2] = 0;
        resp[3] = 0;
        *resp_len = 4;
        return SCMI_SUCCESS;

    case 0x2: /* PROTOCOL_MESSAGE_ATTRIBUTES */
        if (req_len < 4) {
            return SCMI_ERR_PROTOCOL;
        }
        id = ldl_le_p(req);
        if (id > 0x8 && id != 0xb) {
            return SCMI_ERR_NOT_FOUND;
        }
        stl_le_p(resp, 0);
        *resp_len = 4;
        return SCMI_SUCCESS;

    case 0x3: /* CLOCK_ATTRIBUTES */
        if (req_len < 4) {
            return SCMI_ERR_PROTOCOL;
        }
        id = ldl_le_p(req);
        if (id >= ARRAY_SIZE(rk3588_scmi_clocks)) {
            return SCMI_ERR_NOT_FOUND;
        }
        stl_le_p(resp, SCMI_CLOCK_EXTENDED_NAMES |
                       (s->clock_config[id] & SCMI_CLOCK_ENABLED));
        rk3588_scmi_put_name(resp + 4, rk3588_scmi_clocks[id].name);
        stl_le_p(resp + 4 + RK3588_SCMI_SHORT_NAME_SIZE, 0);
        *resp_len = 4 + RK3588_SCMI_SHORT_NAME_SIZE + 4;
        return SCMI_SUCCESS;

    case 0x4: { /* CLOCK_DESCRIBE_RATES */
        uint32_t rate_index;
        size_t count;

        if (req_len < 8) {
            return SCMI_ERR_PROTOCOL;
        }
        id = ldl_le_p(req);
        rate_index = ldl_le_p(req + 4);
        if (id >= ARRAY_SIZE(rk3588_scmi_clocks)) {
            return SCMI_ERR_NOT_FOUND;
        }
        count = rk3588_scmi_clocks[id].num_rates;
        if (rate_index >= count) {
            return SCMI_ERR_OUT_OF_RANGE;
        }
        count -= rate_index;
        stl_le_p(resp, count);
        for (size_t i = 0; i < count; i++) {
            stq_le_p(resp + 4 + i * 8,
                     rk3588_scmi_clocks[id].rates[rate_index + i]);
        }
        *resp_len = 4 + count * 8;
        return SCMI_SUCCESS;
    }

    case 0x5: { /* CLOCK_RATE_SET */
        uint32_t flags;
        uint64_t rate;
        int32_t status;

        if (req_len < 16) {
            return SCMI_ERR_PROTOCOL;
        }
        flags = ldl_le_p(req);
        id = ldl_le_p(req + 4);
        if (id >= ARRAY_SIZE(rk3588_scmi_clocks)) {
            return SCMI_ERR_NOT_FOUND;
        }
        if (flags & ~(SCMI_CLOCK_SET_ASYNC | SCMI_CLOCK_SET_IGNORE_RESP |
                      SCMI_CLOCK_SET_ROUND_UP | SCMI_CLOCK_SET_ROUND_AUTO)) {
            return SCMI_ERR_PARAMS;
        }
        if (flags & SCMI_CLOCK_SET_ASYNC) {
            return SCMI_ERR_SUPPORT;
        }
        rate = ldq_le_p(req + 8);
        status = rk3588_scmi_select_rate(id, flags, rate, &rate);
        if (status != SCMI_SUCCESS) {
            return status;
        }
        s->clock_rate[id] = rate;
        if (id == RK3588_SCMI_CPUL) {
            /* TF-A v2.12 also sets DSU to the requested CPUL rate. */
            s->clock_rate[RK3588_SCMI_DSU] = rate;
        }
        *resp_len = 0;
        return SCMI_SUCCESS;
    }

    case 0x6: /* CLOCK_RATE_GET */
        if (req_len < 4) {
            return SCMI_ERR_PROTOCOL;
        }
        id = ldl_le_p(req);
        if (id >= ARRAY_SIZE(rk3588_scmi_clocks)) {
            return SCMI_ERR_NOT_FOUND;
        }
        stq_le_p(resp, s->clock_rate[id]);
        *resp_len = 8;
        return SCMI_SUCCESS;

    case 0x7: { /* CLOCK_CONFIG_SET */
        uint32_t attributes;

        if (req_len < 8) {
            return SCMI_ERR_PROTOCOL;
        }
        id = ldl_le_p(req);
        if (id >= ARRAY_SIZE(rk3588_scmi_clocks)) {
            return SCMI_ERR_NOT_FOUND;
        }
        attributes = ldl_le_p(req + 4);
        if (attributes & ~SCMI_CLOCK_ENABLED) {
            return SCMI_ERR_PARAMS;
        }
        s->clock_config[id] = attributes;
        *resp_len = 0;
        return SCMI_SUCCESS;
    }

    case 0x8: /* CLOCK_NAME_GET */
        if (req_len < 4) {
            return SCMI_ERR_PROTOCOL;
        }
        id = ldl_le_p(req);
        if (id >= ARRAY_SIZE(rk3588_scmi_clocks)) {
            return SCMI_ERR_NOT_FOUND;
        }
        memset(resp, 0, 4 + RK3588_SCMI_MAX_STR_SIZE);
        memcpy(resp + 4, rk3588_scmi_clocks[id].name,
               strlen(rk3588_scmi_clocks[id].name));
        *resp_len = 4 + RK3588_SCMI_MAX_STR_SIZE;
        return SCMI_SUCCESS;

    case 0xb: { /* CLOCK_CONFIG_GET */
        uint32_t flags;

        if (req_len < 8) {
            return SCMI_ERR_PROTOCOL;
        }
        id = ldl_le_p(req);
        if (id >= ARRAY_SIZE(rk3588_scmi_clocks)) {
            return SCMI_ERR_NOT_FOUND;
        }
        flags = ldl_le_p(req + 4);
        if (flags & ~0xffU) {
            return SCMI_ERR_PARAMS;
        }
        if (flags) { /* OEM configuration type */
            return SCMI_ERR_SUPPORT;
        }
        stl_le_p(resp, 0);
        stl_le_p(resp + 4, s->clock_config[id]);
        stl_le_p(resp + 8, 0);
        *resp_len = 12;
        return SCMI_SUCCESS;
    }

    default:
        return SCMI_ERR_SUPPORT;
    }
}

static void rk3588_scmi_process(RK3588SCMIState *s)
{
    uint32_t channel_status = rk3588_scmi_rd32(s, SCMI_SHMEM_CHANNEL_STATUS);
    uint32_t length = rk3588_scmi_rd32(s, SCMI_SHMEM_LENGTH);
    uint32_t header = rk3588_scmi_rd32(s, SCMI_SHMEM_MSG_HEADER);
    uint32_t protocol = SCMI_MSG_PROTOCOL(header);
    uint32_t msg_id = SCMI_MSG_ID(header);
    const uint8_t *req = s->shmem_ptr + SCMI_SHMEM_MSG_PAYLOAD;
    uint8_t resp[RK3588_SCMI_RESP_MAX];
    size_t req_len;
    size_t resp_len = 0;
    int32_t status;

    memset(resp, 0, sizeof(resp));

    if (channel_status & SCMI_SHMEM_CHANNEL_ERROR) {
        return;
    }

    if (length < 4 ||
        length - 4 > RK3588_SCMI_SHMEM_SIZE - SCMI_SHMEM_MSG_PAYLOAD) {
        status = SCMI_ERR_PROTOCOL;
        resp_len = 0;
    } else {
        req_len = length - 4;

        switch (protocol) {
        case SCMI_PROTOCOL_BASE:
            status = rk3588_scmi_base_protocol(s, msg_id, req, req_len,
                                               resp, &resp_len);
            break;
        case SCMI_PROTOCOL_CLOCK:
            status = rk3588_scmi_clock_protocol(s, msg_id, req, req_len,
                                                resp, &resp_len);
            break;
        default:
            status = SCMI_ERR_SUPPORT;
            resp_len = 0;
            break;
        }
    }

    if (resp_len > RK3588_SCMI_RESP_MAX) {
        status = SCMI_ERR_PROTOCOL;
        resp_len = 0;
    }

    rk3588_scmi_wr32(s, SCMI_SHMEM_MSG_HEADER, header);
    rk3588_scmi_wr32(s, SCMI_SHMEM_LENGTH, 8 + resp_len);
    rk3588_scmi_wr32(s, SCMI_SHMEM_MSG_PAYLOAD, (uint32_t)status);
    if (resp_len) {
        memcpy(s->shmem_ptr + SCMI_SHMEM_MSG_PAYLOAD + 4, resp, resp_len);
    }
    rk3588_scmi_wr32(s, SCMI_SHMEM_CHANNEL_STATUS,
                     SCMI_SHMEM_CHANNEL_FREE |
                     (status == SCMI_ERR_PROTOCOL ?
                      SCMI_SHMEM_CHANNEL_ERROR : 0));
}

static bool rk3588_scmi_smc_handler(const uint64_t args[8], uint64_t *ret,
                                  void *opaque)
{
    RK3588SCMIState *s = RK3588_SCMCI(opaque);

    rk3588_scmi_process(s);
    *ret = 0;
    return true;
}

static void rk3588_scmi_reset_hold(Object *obj, ResetType type)
{
    RK3588SCMIState *s = RK3588_SCMCI(obj);

    memset(s->shmem_ptr, 0, RK3588_SCMI_SHMEM_SIZE);
    rk3588_scmi_wr32(s, SCMI_SHMEM_CHANNEL_STATUS, SCMI_SHMEM_CHANNEL_FREE);
    for (size_t i = 0; i < ARRAY_SIZE(rk3588_scmi_clocks); i++) {
        s->clock_rate[i] = RK3588_SCMI_BOOT_RATE;
        s->clock_config[i] = 0;
    }
}

static void rk3588_scmi_realize(DeviceState *dev, Error **errp)
{
    RK3588SCMIState *s = RK3588_SCMCI(dev);

    if (arm_register_smc_handler(RK3588_SCMI_SMC_ID, rk3588_scmi_smc_handler,
                                 s) < 0) {
        error_setg(errp, "rk3588-scmci: SMC function is already registered");
        return;
    }
    s->smc_registered = true;
}

static void rk3588_scmi_unrealize(DeviceState *dev)
{
    RK3588SCMIState *s = RK3588_SCMCI(dev);

    if (s->smc_registered) {
        arm_unregister_smc_handler(RK3588_SCMI_SMC_ID,
                                   rk3588_scmi_smc_handler);
        s->smc_registered = false;
    }
}

static void rk3588_scmi_instance_init(Object *obj)
{
    RK3588SCMIState *s = RK3588_SCMCI(obj);

    memory_region_init_ram(&s->shmem, obj, TYPE_RK3588_SCMCI,
                           RK3588_SCMI_SHMEM_SIZE, &error_fatal);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->shmem);
    s->shmem_ptr = memory_region_get_ram_ptr(&s->shmem);
}

static void rk3588_scmi_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    rc->phases.hold = rk3588_scmi_reset_hold;
    dc->realize = rk3588_scmi_realize;
    dc->unrealize = rk3588_scmi_unrealize;
    dc->desc = "Rockchip RK3588 SCMI server";
    dc->user_creatable = false;
}

static const TypeInfo rk3588_scmi_typeinfo = {
    .name = TYPE_RK3588_SCMCI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RK3588SCMIState),
    .instance_init = rk3588_scmi_instance_init,
    .class_init = rk3588_scmi_class_init,
};

static void rk3588_scmi_register_types(void)
{
    type_register_static(&rk3588_scmi_typeinfo);
}

type_init(rk3588_scmi_register_types)
