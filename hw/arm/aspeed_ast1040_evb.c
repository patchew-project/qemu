/*
 * ASPEED AST1040 EVB
 *
 * Copyright (C) 2026 ASPEED Technology Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/arm/boot.h"
#include "hw/arm/machines-qom.h"
#include "hw/arm/aspeed.h"
#include "hw/arm/aspeed_soc.h"
#include "hw/core/qdev-clock.h"
#include "hw/misc/aspeed_cptra_mbox.h"
#include "system/system.h"

#define AST1040_INTERNAL_FLASH_SIZE (4 * MiB)
/* Main SYSCLK frequency in Hz (400MHz) */
#define SYSCLK_FRQ 400000000ULL

#define TYPE_AST1040_EVB_MACHINE MACHINE_TYPE_NAME("ast1040-evb")
OBJECT_DECLARE_SIMPLE_TYPE(Ast1040EvbMachineState, AST1040_EVB_MACHINE)

struct Ast1040EvbMachineState {
    AspeedMachineState parent_obj;

    char *cptra_peer;
    Notifier machine_done;
};

static void aspeed_bic_machine_done(Notifier *notifier, void *data)
{
    Ast1040EvbMachineState *m = container_of(notifier,
                                             Ast1040EvbMachineState,
                                             machine_done);
    AspeedMachineState *bmc = ASPEED_MACHINE(m);
    Aspeed1040SoCState *a1040 = ASPEED1040_SOC(bmc->soc);
    bool ambiguous = false;
    Object *peer;
    Error *err = NULL;

    if (!m->cptra_peer) {
        return;
    }

    peer = object_resolve_path_type(m->cptra_peer, TYPE_CPTRA_MBOX_PEER,
                                    &ambiguous);
    if (!peer || ambiguous) {
        error_report("cptra-peer: peer '%s' not found%s",
                     m->cptra_peer, ambiguous ? " (ambiguous)" : "");
        exit(1);
    }

    if (!aspeed_cptra_mbox_set_peer(&a1040->cptra_mbox,
                                    CPTRA_MBOX_PEER(peer), &err)) {
        error_report_err(err);
        exit(1);
    }
}

static void aspeed_bic_machine_init(MachineState *machine)
{
    AspeedMachineState *bmc = ASPEED_MACHINE(machine);
    AspeedMachineClass *amc = ASPEED_MACHINE_GET_CLASS(machine);
    Clock *sysclk;

    sysclk = clock_new(OBJECT(machine), "SYSCLK");
    clock_set_hz(sysclk, SYSCLK_FRQ);

    bmc->soc = ASPEED_SOC(object_new(amc->soc_name));
    object_property_add_child(OBJECT(machine), "soc", OBJECT(bmc->soc));
    object_unref(OBJECT(bmc->soc));
    qdev_connect_clock_in(DEVICE(bmc->soc), "sysclk", sysclk);

    object_property_set_link(OBJECT(bmc->soc), "memory",
                             OBJECT(get_system_memory()), &error_abort);
    aspeed_connect_serial_hds_to_uarts(bmc);
    qdev_realize(DEVICE(bmc->soc), NULL, &error_abort);

    if (AST1040_EVB_MACHINE(machine)->cptra_peer) {
        AST1040_EVB_MACHINE(machine)->machine_done.notify =
            aspeed_bic_machine_done;
        qemu_add_machine_init_done_notifier(
            &AST1040_EVB_MACHINE(machine)->machine_done);
    }

    armv7m_load_kernel(ARM_CPU(first_cpu),
                       machine->kernel_filename,
                       0,
                       AST1040_INTERNAL_FLASH_SIZE);
}

static char *aspeed_bic_get_cptra_peer(Object *obj, Error **errp)
{
    Ast1040EvbMachineState *m = AST1040_EVB_MACHINE(obj);

    return g_strdup(m->cptra_peer);
}

static void aspeed_bic_set_cptra_peer(Object *obj, const char *value,
                                      Error **errp)
{
    Ast1040EvbMachineState *m = AST1040_EVB_MACHINE(obj);

    g_free(m->cptra_peer);
    m->cptra_peer = g_strdup(value);
}

static void aspeed_machine_ast1040_evb_class_init(ObjectClass *oc,
                                                  const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(oc);

    mc->desc = "Aspeed AST1040 BIC EVB (Cortex-M4F)";
    amc->soc_name = "ast1040-a0";
    amc->hw_strap1 = 0;
    amc->hw_strap2 = 0;
    mc->init = aspeed_bic_machine_init;
    mc->default_ram_size = 0;
    amc->macs_mask = 0;
    amc->uart_default = ASPEED_DEV_UART12;
    aspeed_machine_class_init_cpus_defaults(mc);

    object_class_property_add_str(oc, "cptra-peer",
                                  aspeed_bic_get_cptra_peer,
                                  aspeed_bic_set_cptra_peer);
    object_class_property_set_description(oc, "cptra-peer",
        "Caliptra mailbox peer object id");
}

static const TypeInfo aspeed_ast1040_evb_types[] = {
    {
        .name           = TYPE_AST1040_EVB_MACHINE,
        .parent         = TYPE_ASPEED_MACHINE,
        .instance_size  = sizeof(Ast1040EvbMachineState),
        .class_init     = aspeed_machine_ast1040_evb_class_init,
        .interfaces     = arm_machine_interfaces,
    }
};

DEFINE_TYPES(aspeed_ast1040_evb_types)
