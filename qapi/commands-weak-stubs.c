/*
 * Weak symbols for target specific commands
 *
 * Copyright Linaro, 2025
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include <glib.h>

#define NOT_REACHABLE(symbol)                                                  \
void __attribute__((weak)) symbol(void);                                       \
void __attribute__((weak)) symbol(void) { g_assert_not_reached(); }

#define WEAK_STUB(command)                                                     \
NOT_REACHABLE(qmp_marshal_##command)                                           \
NOT_REACHABLE(qmp_##command)

WEAK_STUB(query_cpu_model_comparison);
WEAK_STUB(query_cpu_model_baseline);
WEAK_STUB(set_cpu_topology);
WEAK_STUB(query_s390x_cpu_polarization);
WEAK_STUB(rtc_reset_reinjection);
WEAK_STUB(query_sev);
WEAK_STUB(query_sev_launch_measure);
WEAK_STUB(query_sev_capabilities);
WEAK_STUB(sev_inject_launch_secret);
WEAK_STUB(query_sev_attestation_report);
WEAK_STUB(query_sgx);
WEAK_STUB(query_sgx_capabilities);
WEAK_STUB(xen_event_list);
WEAK_STUB(xen_event_inject);
WEAK_STUB(query_cpu_model_expansion);
WEAK_STUB(query_cpu_definitions);
WEAK_STUB(query_gic_capabilities);
