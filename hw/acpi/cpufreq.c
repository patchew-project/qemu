/*
 * ACPI CPPC register device
 *
 * Support for showing CPU frequency in guest OS.
 *
 * Copyright (c) 2019 HUAWEI TECHNOLOGIES CO.,LTD.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "chardev/char.h"
#include "qemu/log.h"
#include "trace.h"
#include "qemu/option.h"
#include "sysemu/sysemu.h"
#include "hw/acpi/acpi-defs.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "hw/boards.h"

#define TYPE_CPUFREQ "cpufreq"
#define CPUFREQ(obj) OBJECT_CHECK(CpufreqState, (obj), TYPE_CPUFREQ)
#define NOMINAL_FREQ_FILE "/sys/devices/system/cpu/cpu0/acpi_cppc/nominal_freq"
#define CPU_MAX_FREQ_FILE "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq"
#define HZ_MAX_LENGTH 1024
#define MAX_SUPPORT_SPACE 0x10000

typedef struct CpufreqState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t HighestPerformance;
    uint32_t NominalPerformance;
    uint32_t LowestNonlinearPerformance;
    uint32_t LowestPerformance;
    uint32_t GuaranteedPerformance;
    uint32_t DesiredPerformance;
    uint64_t ReferencePerformanceCounter;
    uint64_t DeliveredPerformanceCounter;
    uint32_t PerformanceLimited;
    uint32_t LowestFreq;
    uint32_t NominalFreq;
    uint32_t reg_size;
} CpufreqState;


static uint64_t cpufreq_read(void *opaque, hwaddr offset,
                           unsigned size)
{
    CpufreqState *s = (CpufreqState *)opaque;
    uint64_t r;
    uint64_t n;

    MachineState *ms = MACHINE(qdev_get_machine());
    unsigned int smp_cpus = ms->smp.cpus;

    if (offset >= smp_cpus * CPPC_REG_PER_CPU_STRIDE) {
        warn_report("cpufreq_read: offset 0x%lx out of range", offset);
        return 0;
    }

    n = offset % CPPC_REG_PER_CPU_STRIDE;
    switch (n) {
    case 0:
        r = s->HighestPerformance;
        break;
    case 4:
        r = s->NominalPerformance;
        break;
    case 8:
        r = s->LowestNonlinearPerformance;
        break;
    case 12:
        r = s->LowestPerformance;
        break;
    case 16:
        r = s->GuaranteedPerformance;
        break;
    case 20:
        r = s->DesiredPerformance;
        break;
    /*
     * We don't have real counters and it is hard to emulate, so always set the
     * counter value to 1 to rely on Linux to use the DesiredPerformance value
     * directly.
     */
    case 24:
        r = s->ReferencePerformanceCounter;
        break;
    /*
     * Guest may still access the register by 32bit; add the process to
     * eliminate unnecessary warnings
     */
    case 28:
        r = s->ReferencePerformanceCounter >> 32;
        break;
    case 32:
        r = s->DeliveredPerformanceCounter;
        break;
    case 36:
        r = s->DeliveredPerformanceCounter >> 32;
        break;

    case 40:
        r = s->PerformanceLimited;
        break;
    case 44:
        r = s->LowestFreq;
        break;
    case 48:
        r = s->NominalFreq;
        break;
    default:
        error_printf("cpufreq_read: Bad offset 0x%lx\n", offset);
        r = 0;
        break;
    }
    return r;
}

static void cpufreq_write(void *opaque, hwaddr offset,
                           uint64_t value, unsigned size)
{
    uint64_t n;

    MachineState *ms = MACHINE(qdev_get_machine());
    unsigned int smp_cpus = ms->smp.cpus;

    if (offset >= smp_cpus * CPPC_REG_PER_CPU_STRIDE) {
        error_printf("cpufreq_write: offset 0x%lx out of range", offset);
        return;
    }

    n = offset % CPPC_REG_PER_CPU_STRIDE;

    switch (n) {
    case 20:
        break;
    default:
        error_printf("cpufreq_write: Bad offset 0x%lx\n", offset);
    }
}

static uint32_t CPPC_Read(const char *hostpath)
{
    int fd;
    char buffer[HZ_MAX_LENGTH] = { 0 };
    uint64_t hz;
    int len;
    const char *endptr = NULL;
    int ret;

    fd = qemu_open(hostpath, O_RDONLY);
    if (fd < 0) {
        return 0;
    }

    len = read(fd, buffer, HZ_MAX_LENGTH);
    qemu_close(fd);
    if (len <= 0) {
        return 0;
    }
    ret = qemu_strtoul(buffer, &endptr, 0, &hz);
    if (ret < 0) {
        return 0;
    }
    return (uint32_t)hz;
}

static const MemoryRegionOps cpufreq_ops = {
    .read = cpufreq_read,
    .write = cpufreq_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void hz_init(CpufreqState *s)
{
    uint32_t hz;

    hz = CPPC_Read(NOMINAL_FREQ_FILE);
    if (hz == 0) {
        hz = CPPC_Read(CPU_MAX_FREQ_FILE);
        /* Value in CpuMaxFrequency is in KHz unit; convert to MHz */
        hz = hz / 1000;
    }

    s->HighestPerformance = hz;
    s->NominalPerformance = hz;
    s->LowestNonlinearPerformance = hz;
    s->LowestPerformance = hz;
    s->GuaranteedPerformance = hz;
    s->DesiredPerformance = hz;
    s->ReferencePerformanceCounter = 1;
    s->DeliveredPerformanceCounter = 1;
    s->PerformanceLimited = 0;
    s->LowestFreq = hz;
    s->NominalFreq = hz;
}

static void cpufreq_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    CpufreqState *s = CPUFREQ(obj);

    MachineState *ms = MACHINE(qdev_get_machine());
    unsigned int smp_cpus = ms->smp.cpus;

    s->reg_size = smp_cpus * CPPC_REG_PER_CPU_STRIDE;
    if (s->reg_size > MAX_SUPPORT_SPACE) {
        error_report("Required space 0x%x excesses the maximun size 0x%x",
                 s->reg_size, MAX_SUPPORT_SPACE);
        abort();
    }

    memory_region_init_io(&s->iomem, OBJECT(s), &cpufreq_ops, s, "cpufreq",
                          s->reg_size);
    sysbus_init_mmio(sbd, &s->iomem);
    hz_init(s);
    return;
}

static const TypeInfo cpufreq_info = {
    .name          = TYPE_CPUFREQ,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(CpufreqState),
    .instance_init = cpufreq_init,
};

static void cpufreq_register_types(void)
{
    type_register_static(&cpufreq_info);
}

type_init(cpufreq_register_types)
