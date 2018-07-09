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
#include "pci-pc.h"

typedef struct QX86_64_PCMachine QX86_64_PCMachine;
typedef struct i440FX_pcihost i440FX_pcihost;
typedef struct QSDHCI_PCI  QSDHCI_PCI;

struct i440FX_pcihost {
    QOSGraphObject obj;
    QPCIBusPC pci;
};

struct QX86_64_PCMachine {
    QOSGraphObject obj;
    i440FX_pcihost bridge;
};

/* i440FX_pcihost */

static QOSGraphObject *i440FX_host_get_device(void *obj, const char *device)
{
    i440FX_pcihost *host = obj;
    if (!g_strcmp0(device, "pci-bus-pc")) {
        return &host->pci.obj;
    }
    printf("%s not present in i440FX-pcihost", device);
    abort();
}

static void qos_create_i440FX_host(i440FX_pcihost *host)
{
    host->obj.get_device = i440FX_host_get_device;
    qpci_set_pc(&host->pci, global_qtest, NULL);
}

/* x86_64/pc machine */

static void pc_destroy(QOSGraphObject *obj)
{
    g_free(obj);
}

static QOSGraphObject *pc_get_device(void *obj, const char *device)
{
    QX86_64_PCMachine *machine = obj;
    if (!g_strcmp0(device, "i440FX-pcihost")) {
        return &machine->bridge.obj;
    }

    printf("%s not present in x86_64/pc", device);
    abort();
}

static void *qos_create_machine_pc(void)
{
    QX86_64_PCMachine *machine = g_new0(QX86_64_PCMachine, 1);
    machine->obj.get_device = pc_get_device;
    machine->obj.destructor = pc_destroy;
    qos_create_i440FX_host(&machine->bridge);

    return &machine->obj;
}

static void pc_machine(void)
{
    qos_node_create_machine("x86_64/pc", qos_create_machine_pc);
    qos_node_create_driver("i440FX-pcihost", NULL);
    qos_node_contains("x86_64/pc", "i440FX-pcihost");
    qos_node_contains("i440FX-pcihost", "pci-bus-pc");
}

libqos_init(pc_machine);
