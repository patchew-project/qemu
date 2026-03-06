/*
 * Axiado SoC AX3000
 *
 * Author: Kuan-Jui Chiu <kchiu@axiado.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "system/address-spaces.h"
#include "hw/arm/bsa.h"
#include "hw/arm/axiado-soc.h"
#include "system/system.h"
#include "qobject/qlist.h"
#include "qom/object.h"

static uint64_t sdhci_phy_read(void *opaque, hwaddr offset, unsigned size)
{
    uint32_t val = 0x00;

    switch (offset) {
    case REG_ID:
        val = 0x3dff6870;
        break;
    case REG_STATUS:
        // Make DLL_RDY | CAL_DONE
        val =  (1u << 0) | (1u << 6);
        break;
    default:
        break;
    }

    return val;
}

static void sdhci_phy_write(void *opaque, hwaddr offset, uint64_t val, unsigned size)
{
    /* TBD */
}

static const MemoryRegionOps sdhci_phy_ops = {
    .read = sdhci_phy_read,
    .write = sdhci_phy_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static uint64_t timer_ctrl_read(void *opaque, hwaddr offset, unsigned size)
{
    return 0x0;
}

static void timer_ctrl_write(void *opaque, hwaddr offset, uint64_t val, unsigned size)
{
    /* TBD */
}

static const MemoryRegionOps timer_ctrl_ops = {
    .read = timer_ctrl_read,
    .write = timer_ctrl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static uint64_t pll_read(void *opaque, hwaddr offset, unsigned size)
{
    switch (offset) {
    case CLKRST_CPU_PLL_POSTDIV_OFFSET:
        return 0x20891b;
    case CLKRST_CPU_PLL_STS_OFFSET:
        return 0x01;
    default:
        return 0x00;
    }
}

static void pll_write(void *opaque, hwaddr offset, uint64_t val, unsigned size)
{
    /* TBD */
}

static const MemoryRegionOps pll_ops = {
    .read = pll_read,
    .write = pll_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void ax3000_init(Object *obj)
{
    AxiadoSoCState *s = AXIADO_SOC(obj);
    AxiadoSoCClass *sc = AXIADO_SOC_GET_CLASS(s);
    int i;

    for (i = 0; i < sc->num_cpus; i++) {
        g_autofree char *name = g_strdup_printf("cpu%d", i);
        object_initialize_child(obj, name, &s->cpu[i], ARM_CPU_TYPE_NAME("cortex-a53"));
    }

    object_initialize_child(obj, "gic", &s->gic, gicv3_class_name());

    for (i = 0; i < AX3000_NUM_UARTS; i++) {
        g_autofree char *name = g_strdup_printf("uart%d", i);
        object_initialize_child(obj, name, &s->uart[i], TYPE_CADENCE_UART);
    }

    object_initialize_child(obj, "sdhci0", &s->sdhci0, TYPE_SYSBUS_SDHCI);
}

static void ax3000_realize(DeviceState *dev, Error **errp)
{
    AxiadoSoCState *s = AXIADO_SOC(dev);
    AxiadoSoCClass *sc = AXIADO_SOC_GET_CLASS(s);
    SysBusDevice *gicsbd = SYS_BUS_DEVICE(&s->gic);
    DeviceState *gicdev = DEVICE(&s->gic);
    QList *redist_region_count;
    int i;

    /* CPUs */
    for (i = 0; i < sc->num_cpus; i++) {
        object_property_set_int(OBJECT(&s->cpu[i]), "cntfrq", 8000000,
                                &error_abort);

        if (object_property_find(OBJECT(&s->cpu[i]), "has_el3")) {
            object_property_set_bool(OBJECT(&s->cpu[i]), "has_el3",
                                     false, &error_abort);
        }

        if (!qdev_realize(DEVICE(&s->cpu[i]), NULL, errp)) {
            return;
        }
    }

    /* GIC */
    qdev_prop_set_uint32(gicdev, "num-cpu", sc->num_cpus);
    qdev_prop_set_uint32(gicdev, "num-irq",
                         AX3000_NUM_IRQS + GIC_INTERNAL);

    redist_region_count = qlist_new();
    qlist_append_int(redist_region_count, sc->num_cpus);
    qdev_prop_set_array(gicdev, "redist-region-count", redist_region_count);

    if (!sysbus_realize(gicsbd, errp)) {
        return;
    }

    sysbus_mmio_map(gicsbd, 0, AX3000_GIC_DIST_BASE);
    sysbus_mmio_map(gicsbd, 1, AX3000_GIC_REDIST_BASE);

    /*
     * Wire the outputs from each CPU's generic timer and the GICv3
     * maintenance interrupt signal to the appropriate GIC PPI inputs, and
     * the GIC's IRQ/FIQ interrupt outputs to the CPU's inputs.
     */
    for (i = 0; i < sc->num_cpus; i++) {
        DeviceState *cpudev = DEVICE(&s->cpu[i]);
        int intidbase = AX3000_NUM_IRQS + i * GIC_INTERNAL;
        qemu_irq irq;

        /*
         * Mapping from the output timer irq lines from the CPU to the
         * GIC PPI inputs.
         */
        static const int timer_irqs[] = {
            [GTIMER_PHYS] = ARCH_TIMER_NS_EL1_IRQ,
            [GTIMER_VIRT] = ARCH_TIMER_VIRT_IRQ,
            [GTIMER_HYP]  = ARCH_TIMER_NS_EL2_IRQ,
            [GTIMER_SEC]  = ARCH_TIMER_S_EL1_IRQ
        };

        for (int j = 0; j < ARRAY_SIZE(timer_irqs); j++) {
            irq = qdev_get_gpio_in(gicdev, intidbase + timer_irqs[j]);
            qdev_connect_gpio_out(cpudev, j, irq);
        }

        irq = qdev_get_gpio_in(gicdev, intidbase + ARCH_GIC_MAINT_IRQ);
        qdev_connect_gpio_out_named(cpudev, "gicv3-maintenance-interrupt",
                                        0, irq);

        sysbus_connect_irq(gicsbd, i,
                           qdev_get_gpio_in(cpudev, ARM_CPU_IRQ));
        sysbus_connect_irq(gicsbd, i + sc->num_cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_FIQ));
        sysbus_connect_irq(gicsbd, i + 2 * sc->num_cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_VIRQ));
        sysbus_connect_irq(gicsbd, i + 3 * sc->num_cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_VFIQ));
    }

    /* DRAM */
    for (i = 0; i < AX3000_NUM_BANKS; i++) {
        struct {
            hwaddr addr;
            size_t size;
            const char *name;
        } dram_table[] = {
            { AX3000_DRAM0_BASE, AX3000_DRAM0_SIZE, "dram0" },
            { AX3000_DRAM1_BASE, AX3000_DRAM1_SIZE, "dram1" }
        };

        memory_region_init_ram(&s->dram[i], OBJECT(s), dram_table[i].name,
                               dram_table[i].size, &error_fatal);
        memory_region_add_subregion(get_system_memory(), dram_table[i].addr,
                                    &s->dram[i]);
    }

    /* UARTs */
    for (i = 0; i < AX3000_NUM_UARTS; i++) {
        struct {
            hwaddr addr;
            unsigned int irq;
        } serial_table[] = {
            { AX3000_UART0_BASE, AX3000_UART0_IRQ },
            { AX3000_UART1_BASE, AX3000_UART1_IRQ },
            { AX3000_UART2_BASE, AX3000_UART2_IRQ },
            { AX3000_UART3_BASE, AX3000_UART3_IRQ }
        };

        qdev_prop_set_chr(DEVICE(&s->uart[i]), "chardev", serial_hd(i));
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->uart[i]), errp)) {
            return;
        }

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->uart[i]), 0, serial_table[i].addr);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->uart[i]), 0,
                           qdev_get_gpio_in(gicdev, serial_table[i].irq));
    }

    /* Timer control*/
    memory_region_init_io(&s->timer_ctrl, OBJECT(s), &timer_ctrl_ops, s,
                          "timer_ctrl", 32);
    memory_region_add_subregion(get_system_memory(), AX3000_TIMER_CTRL,
                                &s->timer_ctrl);

    /* PLL control */
    memory_region_init_io(&s->pll_ctrl, OBJECT(s), &pll_ops, s, "pll_ctrl",
                          32);
    memory_region_add_subregion(get_system_memory(), AX3000_PLL_BASE,
                                &s->pll_ctrl);

    /* SDHCI */
    qdev_prop_set_uint64(DEVICE(&s->sdhci0), "capareg", 0x216737eed0b0);
    qdev_prop_set_uint64(DEVICE(&s->sdhci0), "sd-spec-version", 3);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->sdhci0), errp)) {
        return;
    }

    sysbus_mmio_map(SYS_BUS_DEVICE(&s->sdhci0), 0, AX3000_SDHCI0_BASE);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->sdhci0), 0,
                       qdev_get_gpio_in(gicdev, AX3000_SDHCI0_IRQ));

    memory_region_init_io(&s->sdhci_phy, OBJECT(s), &sdhci_phy_ops, s,
                           "sdhci_phy", AX3000_SDHCI0_PHY_SIZE);
    memory_region_add_subregion(get_system_memory(), AX3000_SDHCI0_PHY_BASE,
                                &s->sdhci_phy);
}

static void ax3000_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    AxiadoSoCClass *sc = AXIADO_SOC_CLASS(oc);

    dc->desc = "Axiado SoC AX3000";
    dc->realize = ax3000_realize;
    sc->num_cpus = AX3000_NUM_CPUS;
}

static const TypeInfo axiado_soc_types[] = {
    {
        .name           = TYPE_AXIADO_SOC,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(AxiadoSoCState),
        .instance_init  = ax3000_init,
        .class_init     = ax3000_class_init,
    }
};

DEFINE_TYPES(axiado_soc_types)
