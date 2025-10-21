/*
 * RISC-V WorldGuard Device
 *
 * Copyright (c) 2022 SiFive, Inc.
 *
 * This provides WorldGuard global config.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "exec/hwaddr.h"
#include "hw/registerfields.h"
#include "hw/sysbus.h"
#include "hw/hw.h"
#include "hw/qdev-properties.h"
#include "hw/misc/riscv_worldguard.h"
#include "hw/core/cpu.h"
#include "target/riscv/cpu.h"
#include "trace.h"

/*
 * WorldGuard global config:
 * List the global setting of WG, like num-of-worlds. It is unique in the machine.
 * All CPUs with WG extension and wgChecker devices will use it.
 */
struct RISCVWorldGuardState *worldguard_config;

static Property riscv_worldguard_properties[] = {
    DEFINE_PROP_UINT32("nworlds", RISCVWorldGuardState, nworlds, 0),

    /* Only Trusted WID could access wgCheckers if it is enabled. */
    DEFINE_PROP_UINT32("trustedwid", RISCVWorldGuardState, trustedwid, NO_TRUSTEDWID),

    /*
     * WG reset value is bypass mode in HW. All WG permission checkings are
     * pass by default, so SW could correctly run on the machine w/o any WG
     * programming.
     */
    DEFINE_PROP_BOOL("hw-bypass", RISCVWorldGuardState, hw_bypass, false),

    /*
     * TrustZone compatible mode:
     * This mode is only supported in 2 worlds system. It converts WorldGuard
     * WID to TZ NS signal on the bus so WG could be cooperated with
     * TZ components. In QEMU, it converts WID to 'MemTxAttrs.secure' bit used
     * by TZ.
     */
    DEFINE_PROP_BOOL("tz-compat", RISCVWorldGuardState, tz_compat, false),
};

/* WID to MemTxAttrs converter */
void wid_to_mem_attrs(MemTxAttrs *attrs, uint32_t wid)
{
    g_assert(wid < worldguard_config->nworlds);

    attrs->unspecified = 0;
    if (worldguard_config->tz_compat) {
        attrs->secure = wid;
    } else {
        attrs->world_id = wid;
    }
}

/* MemTxAttrs to WID converter */
uint32_t mem_attrs_to_wid(MemTxAttrs attrs)
{
    if (attrs.unspecified) {
        if (worldguard_config->trustedwid != NO_TRUSTEDWID) {
            return worldguard_config->trustedwid;
        } else {
            return worldguard_config->nworlds - 1;
        }
    }

    if (worldguard_config->tz_compat) {
        return attrs.secure;
    } else {
        return attrs.world_id;
    }
}

bool could_access_wgblocks(MemTxAttrs attrs, const char *wgblock)
{
    uint32_t wid = mem_attrs_to_wid(attrs);
    uint32_t trustedwid = worldguard_config->trustedwid;

    if ((trustedwid == NO_TRUSTEDWID) || (wid == trustedwid)) {
        return true;
    } else {
        /*
         * Only Trusted WID could access WG blocks if having it.
         * Access them from other WIDs will get failed.
         */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Invalid access to %s from non-trusted WID %d\n",
                      __func__, wgblock, wid);

        return false;
    }
}

static void riscv_worldguard_realize(DeviceState *dev, Error **errp)
{
    RISCVWorldGuardState *s = RISCV_WORLDGUARD(dev);

    if (worldguard_config != NULL) {
        error_setg(errp, "Couldn't realize multiple global WorldGuard configs.");
        return;
    }

    if ((s->nworlds) & (s->nworlds - 1)) {
        error_setg(errp, "Current implementation only support power-of-2 NWorld.");
        return;
    }

    if ((s->trustedwid != NO_TRUSTEDWID) && (s->trustedwid >= s->nworlds)) {
        error_setg(errp, "Trusted WID must be less than the number of world.");
        return;
    }

    if ((s->nworlds != 2) && (s->tz_compat)) {
        error_setg(errp, "Only 2 worlds system could use TrustZone compatible mode.");
        return;
    }

    /* Register WG global config */
    worldguard_config = s;

    /* Initialize global data for wgChecker */
    wgc_slot_perm_mask = MAKE_64BIT_MASK(0, 2 * worldguard_config->nworlds);
}

static void riscv_worldguard_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, riscv_worldguard_properties);
    dc->user_creatable = true;
    dc->realize = riscv_worldguard_realize;
}

static const TypeInfo riscv_worldguard_info = {
    .name          = TYPE_RISCV_WORLDGUARD,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(RISCVWorldGuardState),
    .class_init    = riscv_worldguard_class_init,
};

/*
 * Create WorldGuard global config
 */
DeviceState *riscv_worldguard_create(uint32_t nworlds, uint32_t trustedwid,
                                     bool hw_bypass, bool tz_compat)
{
    DeviceState *dev = qdev_new(TYPE_RISCV_WORLDGUARD);
    qdev_prop_set_uint32(dev, "nworlds", nworlds);
    qdev_prop_set_uint32(dev, "trustedwid", trustedwid);
    qdev_prop_set_bit(dev, "hw-bypass", hw_bypass);
    qdev_prop_set_bit(dev, "tz-compat", tz_compat);
    qdev_realize(DEVICE(dev), NULL, &error_fatal);
    return dev;
}

static void riscv_worldguard_register_types(void)
{
    type_register_static(&riscv_worldguard_info);
}

type_init(riscv_worldguard_register_types)
