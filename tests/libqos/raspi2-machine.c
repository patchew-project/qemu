/*
 * libqos driver framework
 *
 * Copyright (c) 2018 Emanuele Giuseppe Esposito <e.emanuelegiuseppe@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qgraph.h"
#include "sdhci.h"

typedef struct QRaspi2Machine QRaspi2Machine;

struct QRaspi2Machine {
    QOSGraphObject obj;
    QSDHCI_MemoryMapped sdhci;
};

static void raspi2_destroy(QOSGraphObject *obj)
{
    g_free(obj);
}

static QOSGraphObject *raspi2_get_device(void *obj, const char *device)
{
    QRaspi2Machine *machine = obj;
    if (!g_strcmp0(device, "generic-sdhci")) {
        return &machine->sdhci.obj;
    }

    printf("%s not present in arm/raspi2", device);
    abort();
}

static void *qos_create_machine_arm_raspi2(void)
{
    QRaspi2Machine *machine = g_new0(QRaspi2Machine, 1);

    machine->obj.get_device = raspi2_get_device;
    machine->obj.destructor = raspi2_destroy;
    qos_create_sdhci_mm(&machine->sdhci, 0x3f300000, &(QSDHCIProperties) {
        .version = 3,
        .baseclock = 52,
        .capab.sdma = false,
        .capab.reg = 0x052134b4
    });
    return &machine->obj;
}

static void raspi2(void)
{
    qos_node_create_machine("arm/raspi2", qos_create_machine_arm_raspi2);
    qos_node_contains("arm/raspi2", "generic-sdhci");
}

libqos_init(raspi2);
