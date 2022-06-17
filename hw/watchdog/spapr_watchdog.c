/*
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "target/ppc/cpu.h"
#include "migration/vmstate.h"
#include "trace.h"

#include "hw/ppc/spapr.h"

/*
 * Bits 47: "leaveOtherWatchdogsRunningOnTimeout", specified on
 * the "Start watchdog" operation,
 * 0 - stop out-standing watchdogs on timeout,
 * 1 - leave outstanding watchdogs running on timeout
 */
#define PSERIES_WDTF_LEAVE_OTHER    PPC_BIT(47)

/*    Bits 48-55: "operation" */
#define PSERIES_WDTF_OP(op)             SETFIELD(PPC_BITMASK(48, 55), 0, (op))
#define PSERIES_WDTF_OP_START           PSERIES_WDTF_OP(0x1)
#define PSERIES_WDTF_OP_STOP            PSERIES_WDTF_OP(0x2)
#define PSERIES_WDTF_OP_QUERY           PSERIES_WDTF_OP(0x3)
#define PSERIES_WDTF_OP_QUERY_LPM       PSERIES_WDTF_OP(0x4)

/*    Bits 56-63: "timeoutAction" */
#define PSERIES_WDTF_ACTION(ac)         SETFIELD(PPC_BITMASK(56, 63), 0, (ac))
#define PSERIES_WDTF_ACTION_HARD_POWER_OFF  PSERIES_WDTF_ACTION(0x1)
#define PSERIES_WDTF_ACTION_HARD_RESTART    PSERIES_WDTF_ACTION(0x2)
#define PSERIES_WDTF_ACTION_DUMP_RESTART    PSERIES_WDTF_ACTION(0x3)
#define PSERIES_WDTF_RESERVED           PPC_BITMASK(0, 46)

/*
 * For the "Query watchdog capabilities" operation, a uint64 structure
 * defined as:
 * Bits 0-15: The minimum supported timeout in milliseconds
 * Bits 16-31: The number of watchdogs supported
 * Bits 32-63: Reserved
 */
#define PSERIES_WDTQ_MIN_TIMEOUT(ms)    SETFIELD(PPC_BITMASK(0, 15), 0, (ms))
#define PSERIES_WDTQ_NUM(n)             SETFIELD(PPC_BITMASK(16, 31), 0, (n))

/*
 * For the "Query watchdog LPM requirement" operation:
 * 1 = The given "watchdogNumber" must be stopped prior to suspending
 * 2 = The given "watchdogNumber" does not have to be stopped prior to
 * suspending
 */
#define PSERIES_WDTQL_STOPPED               1
#define PSERIES_WDTQL_QUERY_NOT_STOPPED     2

#define WDT_MIN_TIMEOUT 1 /* 1ms */

static void watchdog_expired(void *pw)
{
    struct SpaprWatchdog *w = pw;
    CPUState *cs;

    trace_spapr_watchdog_expired(w->num, w->action);
    switch (w->action) {
    case WDT_HARD_POWER_OFF:
        qemu_system_vmstop_request(RUN_STATE_SHUTDOWN);
        break;
    case WDT_HARD_RESTART:
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        break;
    case WDT_DUMP_RESTART:
        CPU_FOREACH(cs) {
            async_run_on_cpu(cs, spapr_do_system_reset_on_cpu, RUN_ON_CPU_NULL);
        }
        break;
    }
}

static target_ulong watchdog_stop(unsigned watchdogNumber,
                                  struct SpaprWatchdog *w)
{
    target_ulong ret = H_NOOP;

    if (timer_pending(&w->timer)) {
        timer_del(&w->timer);
        ret = H_SUCCESS;
    }
    trace_spapr_watchdog_stop(watchdogNumber, ret);

    return ret;
}

static target_ulong h_watchdog(PowerPCCPU *cpu,
                               SpaprMachineState *spapr,
                               target_ulong opcode, target_ulong *args)
{
    target_ulong flags = args[0];
    target_ulong watchdogNumber = args[1];
    target_ulong timeoutInMs = args[2];
    unsigned operation = flags & PSERIES_WDTF_OP(~0);
    unsigned timeoutAction = flags & PSERIES_WDTF_ACTION(~0);
    struct SpaprWatchdog *w;

    if (flags & PSERIES_WDTF_RESERVED) {
        return H_PARAMETER;
    }

    switch (operation) {
    case PSERIES_WDTF_OP_START:
        if (watchdogNumber > ARRAY_SIZE(spapr->wds)) {
            return H_P2;
        }
        if (timeoutInMs <= WDT_MIN_TIMEOUT) {
            return H_P3;
        }

        w = &spapr->wds[watchdogNumber - 1];
        switch (timeoutAction) {
        case PSERIES_WDTF_ACTION_HARD_POWER_OFF:
            w->action = WDT_HARD_POWER_OFF;
            break;
        case PSERIES_WDTF_ACTION_HARD_RESTART:
            w->action = WDT_HARD_RESTART;
            break;
        case PSERIES_WDTF_ACTION_DUMP_RESTART:
            w->action = WDT_DUMP_RESTART;
            break;
        default:
            return H_PARAMETER;
        }
        timer_mod(&w->timer,
                  qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + timeoutInMs);
        trace_spapr_watchdog_start(flags, watchdogNumber, timeoutInMs);
        break;
    case PSERIES_WDTF_OP_STOP:
        if (watchdogNumber == (uint64_t) ~0) {
            int i;

            for (i = 1; i <= ARRAY_SIZE(spapr->wds); ++i) {
                watchdog_stop(i, &spapr->wds[i - 1]);
            }
        } else if (watchdogNumber <= ARRAY_SIZE(spapr->wds)) {
            watchdog_stop(watchdogNumber, &spapr->wds[watchdogNumber - 1]);
        } else {
            return H_P2;
        }
        break;
    case PSERIES_WDTF_OP_QUERY:
        args[0] = PSERIES_WDTQ_MIN_TIMEOUT(WDT_MIN_TIMEOUT) |
            PSERIES_WDTQ_NUM(ARRAY_SIZE(spapr->wds));
        trace_spapr_watchdog_query(args[0]);
        break;
    case PSERIES_WDTF_OP_QUERY_LPM:
        if (watchdogNumber > ARRAY_SIZE(spapr->wds)) {
            return H_P2;
        }
        args[0] = PSERIES_WDTQL_QUERY_NOT_STOPPED;
        trace_spapr_watchdog_query_lpm(args[0]);
        break;
    default:
        return H_PARAMETER;
    }

    return H_SUCCESS;
}

void spapr_watchdog_init(SpaprMachineState *spapr)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(spapr->wds); ++i) {
        char name[16];
        SpaprWatchdog *w = &spapr->wds[i];

        w->num = i + 1;
        snprintf(name, sizeof(name) - 1, "wdt%d", i + 1);
        object_initialize_child_with_props(OBJECT(spapr), name, w,
                                           sizeof(SpaprWatchdog),
                                           TYPE_SPAPR_WDT,
                                           &error_fatal, NULL);
        qdev_realize(DEVICE(w), NULL, &error_fatal);
    }
}

static bool watchdog_needed(void *opaque)
{
    SpaprWatchdog *w = opaque;

    return timer_pending(&w->timer);
}

static const VMStateDescription vmstate_wdt = {
    .name = "spapr_watchdog",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = watchdog_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(action, SpaprWatchdog),
        VMSTATE_TIMER(timer, SpaprWatchdog),
        VMSTATE_END_OF_LIST()
    }
};

static void spapr_wdt_realize(DeviceState *dev, Error **errp)
{
    SpaprWatchdog *w = SPAPR_WDT(dev);

    timer_init_ms(&w->timer, QEMU_CLOCK_VIRTUAL, watchdog_expired, w);

    object_property_add_uint64_ptr(OBJECT(dev), "expire",
                                   (uint64_t *)&w->timer.expire_time,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint8_ptr(OBJECT(dev), "action", &w->action,
                                  OBJ_PROP_FLAG_READ);
}

static void spapr_wdt_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = spapr_wdt_realize;
    dc->vmsd = &vmstate_wdt;
    dc->user_creatable = false;
}

static const TypeInfo spapr_wdt_info = {
    .name          = TYPE_SPAPR_WDT,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(SpaprWatchdog),
    .class_init    = spapr_wdt_class_init,
};

static void spapr_watchdog_register_types(void)
{
    spapr_register_hypercall(H_WATCHDOG, h_watchdog);
    type_register_static(&spapr_wdt_info);
}

type_init(spapr_watchdog_register_types)
