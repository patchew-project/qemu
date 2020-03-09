/*
 * QEMU VMPort emulation
 *
 * Copyright (C) 2007 Hervé Poussineau
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "qemu/osdep.h"
#include "hw/isa/isa.h"
#include "hw/i386/pc.h"
#include "hw/input/i8042.h"
#include "hw/qdev-properties.h"
#include "sysemu/sysemu.h"
#include "sysemu/hw_accel.h"
#include "qemu/log.h"
#include "trace.h"

#define VMPORT_MAGIC   0x564D5868

typedef enum {
   VMX_TYPE_UNSET = 0,
   VMX_TYPE_EXPRESS,    /* Deprecated type used for VMware Express */
   VMX_TYPE_SCALABLE_SERVER,    /* VMware ESX server */
   VMX_TYPE_WGS,        /* Deprecated type used for VMware Server */
   VMX_TYPE_WORKSTATION,
   VMX_TYPE_WORKSTATION_ENTERPRISE /* Deprecated type used for ACE 1.x */
} VMX_Type;

/* vCPU features reported by CMD_GET_VCPU_INFO */
#define VCPU_INFO_SLC64_BIT             0
#define VCPU_INFO_SYNC_VTSCS_BIT        1
#define VCPU_INFO_HV_REPLAY_OK_BIT      2
#define VCPU_INFO_LEGACY_X2APIC_BIT     3
#define VCPU_INFO_RESERVED_BIT          31

#define VMPORT(obj) OBJECT_CHECK(VMPortState, (obj), TYPE_VMPORT)

typedef struct VMPortState {
    ISADevice parent_obj;

    MemoryRegion io;
    VMPortReadFunc *func[VMPORT_ENTRIES];
    void *opaque[VMPORT_ENTRIES];

    uint32_t vmx_version;
    uint8_t vmx_type;
    uint32_t max_time_lag_us;
} VMPortState;

static VMPortState *port_state;

void vmport_register(VMPortCommand command, VMPortReadFunc *func, void *opaque)
{
    assert(command < VMPORT_ENTRIES);
    trace_vmport_register(command, func, opaque);
    port_state->func[command] = func;
    port_state->opaque[command] = opaque;
}

static uint64_t vmport_ioport_read(void *opaque, hwaddr addr,
                                   unsigned size)
{
    VMPortState *s = opaque;
    CPUState *cs = current_cpu;
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    unsigned char command;
    uint32_t eax;

    cpu_synchronize_state(cs);

    eax = env->regs[R_EAX];
    if (eax != VMPORT_MAGIC) {
        eax = UINT32_MAX;
        goto out;
    }

    command = env->regs[R_ECX];
    trace_vmport_command(command);
    if (command >= VMPORT_ENTRIES || !s->func[command]) {
        qemu_log_mask(LOG_UNIMP, "vmport: unknown command %x\n", command);
        eax = UINT32_MAX;
        goto out;
    }

    eax = s->func[command](s->opaque[command], addr);

out:
    /*
     * The call above to cpu_synchronize_state() gets vCPU registers values
     * to QEMU but also cause QEMU to write QEMU vCPU registers values to
     * vCPU implementation (e.g. Accelerator such as KVM) just before
     * resuming guest.
     *
     * Therefore, in order to make IOPort return value propagate to
     * guest EAX, we need to explicitly update QEMU EAX register value.
     */
    cpu->env.regs[R_EAX] = eax;
    return eax;
}

static void vmport_ioport_write(void *opaque, hwaddr addr,
                                uint64_t val, unsigned size)
{
    vmport_ioport_read(opaque, addr, 4);
}

static uint32_t vmport_cmd_get_version(void *opaque, uint32_t addr)
{
    X86CPU *cpu = X86_CPU(current_cpu);

    cpu->env.regs[R_EBX] = VMPORT_MAGIC;
    cpu->env.regs[R_ECX] = port_state->vmx_type;
    return port_state->vmx_version;
}

static uint32_t vmport_cmd_get_bios_uuid(void *opaque, uint32_t addr)
{
    X86CPU *cpu = X86_CPU(current_cpu);
    uint32_t *uuid_parts = (uint32_t*)(qemu_uuid.data);

    cpu->env.regs[R_EAX] = uuid_parts[0];
    cpu->env.regs[R_EBX] = uuid_parts[1];
    cpu->env.regs[R_ECX] = uuid_parts[2];
    cpu->env.regs[R_EDX] = uuid_parts[3];
    return cpu->env.regs[R_EAX];
}

static uint32_t vmport_cmd_ram_size(void *opaque, uint32_t addr)
{
    X86CPU *cpu = X86_CPU(current_cpu);

    cpu->env.regs[R_EBX] = 0x1177;
    return ram_size;
}

static uint32_t vmport_cmd_time(void *opaque, uint32_t addr)
{
    X86CPU *cpu = X86_CPU(current_cpu);
    qemu_timeval tv;

    if (qemu_gettimeofday(&tv) < 0) {
        return UINT32_MAX;
    }

    cpu->env.regs[R_EBX] = (uint32_t)tv.tv_usec;
    cpu->env.regs[R_ECX] = port_state->max_time_lag_us;
    return (uint32_t)tv.tv_sec;
}

static uint32_t vmport_cmd_time_full(void *opaque, uint32_t addr)
{
    X86CPU *cpu = X86_CPU(current_cpu);
    qemu_timeval tv;

    if (qemu_gettimeofday(&tv) < 0) {
        return UINT32_MAX;
    }

    cpu->env.regs[R_ESI] = (uint32_t)((uint64_t)tv.tv_sec >> 32);
    cpu->env.regs[R_EDX] = (uint32_t)tv.tv_sec;
    cpu->env.regs[R_EBX] = (uint32_t)tv.tv_usec;
    cpu->env.regs[R_ECX] = port_state->max_time_lag_us;
    return VMPORT_MAGIC;
}

static uint32_t vmport_cmd_get_vcpu_info(void *opaque, uint32_t addr)
{
    return (1 << VCPU_INFO_RESERVED_BIT);
}

/* vmmouse helpers */
void vmmouse_get_data(uint32_t *data)
{
    X86CPU *cpu = X86_CPU(current_cpu);
    CPUX86State *env = &cpu->env;

    data[0] = env->regs[R_EAX]; data[1] = env->regs[R_EBX];
    data[2] = env->regs[R_ECX]; data[3] = env->regs[R_EDX];
    data[4] = env->regs[R_ESI]; data[5] = env->regs[R_EDI];
}

void vmmouse_set_data(const uint32_t *data)
{
    X86CPU *cpu = X86_CPU(current_cpu);
    CPUX86State *env = &cpu->env;

    env->regs[R_EAX] = data[0]; env->regs[R_EBX] = data[1];
    env->regs[R_ECX] = data[2]; env->regs[R_EDX] = data[3];
    env->regs[R_ESI] = data[4]; env->regs[R_EDI] = data[5];
}

static const MemoryRegionOps vmport_ops = {
    .read = vmport_ioport_read,
    .write = vmport_ioport_write,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void vmport_realizefn(DeviceState *dev, Error **errp)
{
    ISADevice *isadev = ISA_DEVICE(dev);
    VMPortState *s = VMPORT(dev);

    memory_region_init_io(&s->io, OBJECT(s), &vmport_ops, s, "vmport", 1);
    isa_register_ioport(isadev, &s->io, 0x5658);

    port_state = s;
    /* Register some generic port commands */
    vmport_register(VMPORT_CMD_GETVERSION, vmport_cmd_get_version, NULL);
    vmport_register(VMPORT_CMD_GETBIOSUUID, vmport_cmd_get_bios_uuid, NULL);
    vmport_register(VMPORT_CMD_GETRAMSIZE, vmport_cmd_ram_size, NULL);
    vmport_register(VMPORT_CMD_GETTIME, vmport_cmd_time, NULL);
    vmport_register(VMPORT_CMD_GETTIMEFULL, vmport_cmd_time_full, NULL);
    vmport_register(VMPORT_CMD_GET_VCPU_INFO, vmport_cmd_get_vcpu_info, NULL);
}

static Property vmport_properties[] = {
    /* Default value taken from open-vm-tools code VERSION_MAGIC definition */
    DEFINE_PROP_UINT32("vmx-version", VMPortState, vmx_version, 6),
    DEFINE_PROP_UINT8("vmx-type", VMPortState, vmx_type,
                      VMX_TYPE_SCALABLE_SERVER),
    /*
     * Max amount of time lag that can go uncorrected.
     * Value taken from VMware Workstation 5.5.
     **/
    DEFINE_PROP_UINT32("max-time-lag", VMPortState, max_time_lag_us, 1000000),
    DEFINE_PROP_END_OF_LIST(),
};

static void vmport_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = vmport_realizefn;
    /* Reason: realize sets global port_state */
    dc->user_creatable = false;
    device_class_set_props(dc, vmport_properties);
}

static const TypeInfo vmport_info = {
    .name          = TYPE_VMPORT,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(VMPortState),
    .class_init    = vmport_class_initfn,
};

static void vmport_register_types(void)
{
    type_register_static(&vmport_info);
}

type_init(vmport_register_types)
