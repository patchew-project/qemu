/*
 * Tokushudenshikairo TKDN-RX62N-BRD
 *
 * Copyright (c) 2020 Yoshinori Sato
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
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "cpu.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/loader.h"
#include "hw/rx/loader.h"
#include "hw/qdev-properties.h"
#include "hw/rx/rx62n.h"
#include "net/net.h"
#include "hw/net/mii.h"
#include "hw/boards.h"
#include "sysemu/sysemu.h"
#include "sysemu/qtest.h"
#include "sysemu/device_tree.h"

typedef struct {
    /*< private >*/
    MachineState parent_obj;
    /*< public >*/
    RX62NState mcu;
    PHYState phy;
    MDIOState mdio;
} TKDN_RX62NMachineState;

#define TYPE_TKDN_RX62N_MACHINE MACHINE_TYPE_NAME("tkdn-rx62n-brd")

#define TKDN_RX62N_MACHINE(obj) \
    OBJECT_CHECK(TKDN_RX62NMachineState, (obj), TYPE_TKDN_RX62N_MACHINE)

#define TINYBOOT_TOP (0xffffff00)

static void set_bootstrap(hwaddr entry, hwaddr dtb)
{
    /* Minimal hardware initialize for kernel requirement */
    /* linux kernel only works little-endian mode */
    static uint8_t tinyboot[256] = {
        0xfb, 0x2e, 0x20, 0x00, 0x08,       /* mov.l #0x80020, r2 */
        0xf8, 0x2e, 0x00, 0x01, 0x01,       /* mov.l #0x00010100, [r2] */
        0xfb, 0x2e, 0x10, 0x00, 0x08,       /* mov.l #0x80010, r2 */
        0xf8, 0x22, 0xdf, 0x7d, 0xff, 0xff, /* mov.l #0xffff7ddf, [r2] */
        0x62, 0x42,                         /* add #4, r2 */
        0xf8, 0x22, 0xff, 0x7f, 0xff, 0x7f, /* mov.l #0x7fff7fff, [r2] */
        0xfb, 0x2e, 0x40, 0x82, 0x08,       /* mov.l #0x88240, r2 */
        0x3c, 0x22, 0x00,                   /* mov.b #0, 2[r2] */
        0x3c, 0x21, 0x4e,                   /* mov.b #78, 1[r2] */
        0xfb, 0x22, 0x70, 0xff, 0xff, 0xff, /* mov.l #0xffffff70, r2 */
        0xec, 0x21,                         /* mov.l [r2], r1 */
        0xfb, 0x22, 0x74, 0xff, 0xff, 0xff, /* mov.l #0xffffff74, r2 */
        0xec, 0x22,                         /* mov.l [r2], r2 */
        0x7f, 0x02,                         /* jmp r2 */
    };
    int i;

    *((uint32_t *)&tinyboot[0x70]) = cpu_to_le32(dtb);
    *((uint32_t *)&tinyboot[0x74]) = cpu_to_le32(entry);

    /* setup exception trap trampoline */
    for (i = 0; i < 31; i++) {
        *((uint32_t *)&tinyboot[0x80 + i * 4]) = cpu_to_le32(0x10 + i * 4);
    }
    *((uint32_t *)&tinyboot[0xfc]) = cpu_to_le32(TINYBOOT_TOP);
    rom_add_blob_fixed("tinyboot", tinyboot, sizeof(tinyboot), TINYBOOT_TOP);
}

/* Link 100BaseTX-FD */
#define BMSR (MII_BMSR_100TX_FD | MII_BMSR_100TX_HD |                   \
              MII_BMSR_10T_FD | MII_BMSR_10T_HD | MII_BMSR_MFPS |       \
              MII_BMSR_AN_COMP | MII_BMSR_AUTONEG)
#define ANLPAR (MII_ANLPAR_TXFD | MII_ANAR_CSMACD)

static void tkdn_rx62n_net_init(MachineState *m, RX62NState *s,
                                      NICInfo *nd)
{
    TKDN_RX62NMachineState *t = TKDN_RX62N_MACHINE(m);
    object_initialize_child(OBJECT(t), "ether-phy",
                            &t->phy, TYPE_ETHER_PHY);
    qdev_prop_set_uint32(DEVICE(&t->phy), "phy-id", 0x0007c0f0); /* LAN8720A */
    qdev_prop_set_uint32(DEVICE(&t->phy), "link-out-pol", phy_out_p);
    qdev_prop_set_uint16(DEVICE(&t->phy), "bmsr", BMSR);
    qdev_prop_set_uint16(DEVICE(&t->phy), "anlpar", ANLPAR);
    qdev_realize(DEVICE(&t->phy), NULL, &error_abort);

    object_initialize_child(OBJECT(t), "mdio-bb",
                            &t->mdio, TYPE_ETHER_MDIO_BB);
    object_property_set_link(OBJECT(&t->mdio), "phy",
                             OBJECT(&t->phy), &error_abort);
    qdev_prop_set_int32(DEVICE(&t->mdio), "address", 0);
    qdev_realize(DEVICE(&t->mdio), NULL, &error_abort);
}

#define SDRAM_BASE 0x08000000

static void tkdn_rx62n_init(MachineState *machine)
{
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    TKDN_RX62NMachineState *s = TKDN_RX62N_MACHINE(machine);
    RX62NClass *rx62nc;
    MemoryRegion *sysmem = get_system_memory();
    const char *kernel_filename = machine->kernel_filename;
    const char *dtb_filename = machine->dtb;
    rx_kernel_info_t kernel_info;

    if (machine->ram_size < mc->default_ram_size) {
        char *sz = size_to_str(mc->default_ram_size);
        error_report("Invalid RAM size, should be more than %s", sz);
        g_free(sz);
    }

    /* Allocate memory space */
    memory_region_add_subregion(sysmem, SDRAM_BASE, machine->ram);

    /* Initialize MCU */
    object_initialize_child(OBJECT(machine), "mcu",
                            &s->mcu, TYPE_R5F562N8_MCU);
    rx62nc = RX62N_MCU_GET_CLASS(&s->mcu);
    object_property_set_link(OBJECT(&s->mcu), "main-bus", OBJECT(sysmem),
                             &error_abort);
    object_property_set_uint(OBJECT(&s->mcu), "xtal-frequency-hz",
                             12 * 1000 * 1000, &error_abort);
    tkdn_rx62n_net_init(machine, &s->mcu, nd_table);
    object_property_set_link(OBJECT(&s->mcu), "mdiodev",
                             OBJECT(&s->mdio), &error_abort);
    /* Load kernel and dtb */
    if (kernel_filename) {
        kernel_info.ram_start = SDRAM_BASE;
        kernel_info.ram_size = machine->ram_size;
        kernel_info.filename = kernel_filename;
        kernel_info.dtbname = dtb_filename;
        kernel_info.cmdline = machine->kernel_cmdline;
        if (!load_kernel(&kernel_info)) {
            exit(1);
        }
        set_bootstrap(kernel_info.entry, kernel_info.dtb_address);
    } else {
        if (bios_name) {
            if (!load_bios(bios_name, rx62nc->rom_flash_size, &error_abort)) {
                exit(0);
            }
        } else if (!qtest_enabled()) {
            error_report("No bios or kernel specified");
            exit(1);
        }
    }
    qdev_realize(DEVICE(&s->mcu), NULL, &error_abort);
}

static void tkdn_rx62n_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "TokushuDenshiKairo Inc, TKDN-RX62N-BRD";
    mc->init = tkdn_rx62n_init;
    mc->is_default = 0;
    mc->default_cpu_type = TYPE_RX62N_CPU;
    mc->default_ram_size = 16 * MiB;
    mc->default_ram_id = "ext-sdram";
}

static const TypeInfo tkdn_rx62n_type = {
    .name = TYPE_TKDN_RX62N_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size  = sizeof(TKDN_RX62NMachineState),
    .class_init = tkdn_rx62n_class_init,
};

static void tkdn_rx62n_machine_init(void)
{
    type_register_static(&tkdn_rx62n_type);
}

type_init(tkdn_rx62n_machine_init)
