/*
 * Copyright (C) 2010       Citrix Ltd.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"

#include "hw/i386/pc.h"
#include "hw/irq.h"
#include "hw/i386/apic-msidef.h"
#include "hw/xen/xen-x86.h"

#include "hw/xen/xen-hvm-common.h"
#include <xen/hvm/e820.h>
#include "exec/target_page.h"
#include "cpu.h"

static MemoryRegion ram_640k, ram_lo, ram_hi;

/* Compatibility with older version */

/*
 * This allows QEMU to build on a system that has Xen 4.5 or earlier installed.
 * This is here (not in hw/xen/xen_native.h) because xen/hvm/ioreq.h needs to
 * be included before this block and hw/xen/xen_native.h needs to be included
 * before xen/hvm/ioreq.h
 */
#ifndef IOREQ_TYPE_VMWARE_PORT
#define IOREQ_TYPE_VMWARE_PORT  3
struct vmware_regs {
    uint32_t esi;
    uint32_t edi;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
};
typedef struct vmware_regs vmware_regs_t;

struct shared_vmport_iopage {
    struct vmware_regs vcpu_vmport_regs[1];
};
typedef struct shared_vmport_iopage shared_vmport_iopage_t;
#endif

static shared_vmport_iopage_t *shared_vmport_page;

static Notifier suspend;
static Notifier wakeup;

/* Xen specific function for piix pci */

int xen_pci_slot_get_pirq(PCIDevice *pci_dev, int irq_num)
{
    return irq_num + (PCI_SLOT(pci_dev->devfn) << 2);
}

void xen_intx_set_irq(void *opaque, int irq_num, int level)
{
    xen_set_pci_intx_level(xen_domid, 0, 0, irq_num >> 2,
                           irq_num & 3, level);
}

int xen_set_pci_link_route(uint8_t link, uint8_t irq)
{
    return xendevicemodel_set_pci_link_route(xen_dmod, xen_domid, link, irq);
}

int xen_is_pirq_msi(uint32_t msi_data)
{
    /* If vector is 0, the msi is remapped into a pirq, passed as
     * dest_id.
     */
    return ((msi_data & MSI_DATA_VECTOR_MASK) >> MSI_DATA_VECTOR_SHIFT) == 0;
}

void xen_hvm_inject_msi(uint64_t addr, uint32_t data)
{
    xen_inject_msi(xen_domid, addr, data);
}

static void xen_suspend_notifier(Notifier *notifier, void *data)
{
    xc_set_hvm_param(xen_xc, xen_domid, HVM_PARAM_ACPI_S_STATE, 3);
}

/* Xen Interrupt Controller */

static void xen_set_irq(void *opaque, int irq, int level)
{
    xen_set_isa_irq_level(xen_domid, irq, level);
}

qemu_irq *xen_interrupt_controller_init(void)
{
    return qemu_allocate_irqs(xen_set_irq, NULL, 16);
}

/* Memory Ops */

static void xen_ram_init(PCMachineState *pcms,
                         ram_addr_t ram_size, MemoryRegion **ram_memory_p)
{
    X86MachineState *x86ms = X86_MACHINE(pcms);
    MemoryRegion *sysmem = get_system_memory();
    ram_addr_t block_len;
    uint64_t user_lowmem =
        object_property_get_uint(qdev_get_machine(),
                                 PC_MACHINE_MAX_RAM_BELOW_4G,
                                 &error_abort);

    /* Handle the machine opt max-ram-below-4g.  It is basically doing
     * min(xen limit, user limit).
     */
    if (!user_lowmem) {
        user_lowmem = HVM_BELOW_4G_RAM_END; /* default */
    }
    if (HVM_BELOW_4G_RAM_END <= user_lowmem) {
        user_lowmem = HVM_BELOW_4G_RAM_END;
    }

    if (ram_size >= user_lowmem) {
        x86ms->above_4g_mem_size = ram_size - user_lowmem;
        x86ms->below_4g_mem_size = user_lowmem;
    } else {
        x86ms->above_4g_mem_size = 0;
        x86ms->below_4g_mem_size = ram_size;
    }
    if (!x86ms->above_4g_mem_size) {
        block_len = ram_size;
    } else {
        /*
         * Xen does not allocate the memory continuously, it keeps a
         * hole of the size computed above or passed in.
         */
        block_len = (4 * GiB) + x86ms->above_4g_mem_size;
    }
    memory_region_init_ram(&xen_memory, NULL, "xen.ram", block_len,
                           &error_fatal);
    *ram_memory_p = &xen_memory;

    memory_region_init_alias(&ram_640k, NULL, "xen.ram.640k",
                             &xen_memory, 0, 0xa0000);
    memory_region_add_subregion(sysmem, 0, &ram_640k);
    /* Skip of the VGA IO memory space, it will be registered later by the VGA
     * emulated device.
     *
     * The area between 0xc0000 and 0x100000 will be used by SeaBIOS to load
     * the Options ROM, so it is registered here as RAM.
     */
    memory_region_init_alias(&ram_lo, NULL, "xen.ram.lo",
                             &xen_memory, 0xc0000,
                             x86ms->below_4g_mem_size - 0xc0000);
    memory_region_add_subregion(sysmem, 0xc0000, &ram_lo);
    if (x86ms->above_4g_mem_size > 0) {
        memory_region_init_alias(&ram_hi, NULL, "xen.ram.hi",
                                 &xen_memory, 0x100000000ULL,
                                 x86ms->above_4g_mem_size);
        memory_region_add_subregion(sysmem, 0x100000000ULL, &ram_hi);
    }
}

static void regs_to_cpu(vmware_regs_t *vmport_regs, ioreq_t *req)
{
    X86CPU *cpu;
    CPUX86State *env;

    cpu = X86_CPU(current_cpu);
    env = &cpu->env;
    env->regs[R_EAX] = req->data;
    env->regs[R_EBX] = vmport_regs->ebx;
    env->regs[R_ECX] = vmport_regs->ecx;
    env->regs[R_EDX] = vmport_regs->edx;
    env->regs[R_ESI] = vmport_regs->esi;
    env->regs[R_EDI] = vmport_regs->edi;
}

static void regs_from_cpu(vmware_regs_t *vmport_regs)
{
    X86CPU *cpu = X86_CPU(current_cpu);
    CPUX86State *env = &cpu->env;

    vmport_regs->ebx = env->regs[R_EBX];
    vmport_regs->ecx = env->regs[R_ECX];
    vmport_regs->edx = env->regs[R_EDX];
    vmport_regs->esi = env->regs[R_ESI];
    vmport_regs->edi = env->regs[R_EDI];
}

static void handle_vmport_ioreq(XenIOState *state, ioreq_t *req)
{
    vmware_regs_t *vmport_regs;

    assert(shared_vmport_page);
    vmport_regs =
        &shared_vmport_page->vcpu_vmport_regs[state->send_vcpu];
    QEMU_BUILD_BUG_ON(sizeof(*req) < sizeof(*vmport_regs));

    current_cpu = state->cpu_by_vcpu_id[state->send_vcpu];
    regs_to_cpu(vmport_regs, req);
    cpu_ioreq_pio(req);
    regs_from_cpu(vmport_regs);
    current_cpu = NULL;
}

static void xen_wakeup_notifier(Notifier *notifier, void *data)
{
    xc_set_hvm_param(xen_xc, xen_domid, HVM_PARAM_ACPI_S_STATE, 0);
}

void xen_hvm_init_pc(PCMachineState *pcms, MemoryRegion **ram_memory)
{
    MachineState *ms = MACHINE(pcms);
    unsigned int max_cpus = ms->smp.max_cpus;
    int rc;
    xen_pfn_t ioreq_pfn;
    XenIOState *state;

    state = g_new0(XenIOState, 1);

    xen_register_ioreq(state, max_cpus, &xen_memory_listener);

    xen_read_physmap(state);

    suspend.notify = xen_suspend_notifier;
    qemu_register_suspend_notifier(&suspend);

    wakeup.notify = xen_wakeup_notifier;
    qemu_register_wakeup_notifier(&wakeup);

    rc = xen_get_vmport_regs_pfn(xen_xc, xen_domid, &ioreq_pfn);
    if (!rc) {
        DPRINTF("shared vmport page at pfn %lx\n", ioreq_pfn);
        shared_vmport_page =
            xenforeignmemory_map(xen_fmem, xen_domid, PROT_READ|PROT_WRITE,
                                 1, &ioreq_pfn, NULL);
        if (shared_vmport_page == NULL) {
            error_report("map shared vmport IO page returned error %d handle=%p",
                         errno, xen_xc);
            goto err;
        }
    } else if (rc != -ENOSYS) {
        error_report("get vmport regs pfn returned error %d, rc=%d",
                     errno, rc);
        goto err;
    }

    xen_ram_init(pcms, ms->ram_size, ram_memory);

    /* Disable ACPI build because Xen handles it */
    pcms->acpi_build_enabled = false;

    return;

err:
    error_report("xen hardware virtual machine initialisation failed");
    exit(1);
}

void xen_arch_handle_ioreq(XenIOState *state, ioreq_t *req)
{
    switch (req->type) {
    case IOREQ_TYPE_VMWARE_PORT:
            handle_vmport_ioreq(state, req);
        break;
    default:
        hw_error("Invalid ioreq type 0x%x\n", req->type);
    }

    return;
}
