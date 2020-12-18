#include "qemu/osdep.h"
#include "qemu-common.h"
#include <sys/ioctl.h>
#include "qapi/error.h"
#include "hw/ppc/spapr.h"
#include "hw/ppc/spapr_vio.h"
#include "hw/ppc/fdt.h"
#include "sysemu/sysemu.h"
#include "qom/qom-qobject.h"
#include "trace.h"

/* Defined as Big Endian */
struct prom_args {
    uint32_t service;
    uint32_t nargs;
    uint32_t nret;
    uint32_t args[10];
} QEMU_PACKED;

target_ulong spapr_h_vof_client(PowerPCCPU *cpu, SpaprMachineState *spapr,
                                target_ulong opcode, target_ulong *args)
{
    target_ulong of_client_args = ppc64_phys_to_real(args[0]);
    struct prom_args pargs = { 0 };
    char service[64];
    unsigned nargs, nret, i;

    cpu_physical_memory_read(of_client_args, &pargs, sizeof(pargs));
    nargs = be32_to_cpu(pargs.nargs);
    if (nargs >= ARRAY_SIZE(pargs.args)) {
        return H_PARAMETER;
    }

    cpu_physical_memory_read(be32_to_cpu(pargs.service), service,
                             sizeof(service));
    if (strnlen(service, sizeof(service)) == sizeof(service)) {
        /* Too long service name */
        return H_PARAMETER;
    }

    for (i = 0; i < nargs; ++i) {
        pargs.args[i] = be32_to_cpu(pargs.args[i]);
    }

    nret = be32_to_cpu(pargs.nret);
    pargs.args[nargs] = vof_client_call(spapr->fdt_blob, &spapr->vof, service,
                                        pargs.args, nargs,
                                        pargs.args + nargs + 1, nret);
    if (!nret) {
        return H_SUCCESS;
    }

    for (i = 0; i < nret; ++i) {
        pargs.args[nargs + i] = cpu_to_be32(pargs.args[nargs + i]);
    }
    cpu_physical_memory_write(of_client_args + sizeof(uint32_t) * (3 + nargs),
                              pargs.args + nargs, sizeof(uint32_t) * nret);

    return H_SUCCESS;
}

void spapr_vof_client_dt_finalize(SpaprMachineState *spapr, void *fdt)
{
    char *stdout_path = spapr_vio_stdout_path(spapr->vio_bus);

    /* Creates phandles, required by vof_client_open below */
    vof_build_dt(fdt, &spapr->vof);

    /*
     * SLOF-less setup requires an open instance of stdout for early
     * kernel printk. By now all phandles are settled so we can open
     * the default serial console.
     */
    if (stdout_path) {
        _FDT(vof_client_open(fdt, &spapr->vof, "/chosen", "stdout",
                             stdout_path));
    }
}

void spapr_vof_reset(SpaprMachineState *spapr, void *fdt,
                     target_ulong *stack_ptr)
{
    Vof *vof = &spapr->vof;

    *stack_ptr = vof_claim(spapr->fdt_blob, vof, OF_STACK_ADDR, OF_STACK_SIZE,
                           OF_STACK_SIZE);
    if (*stack_ptr == -1) {
        error_report("Memory allocation for stack failed");
        exit(1);
    }
    /*
     * Stack grows downwards and we also reserve here space for
     * the minimum stack frame.
     */
    *stack_ptr += OF_STACK_SIZE - 0x20;

    if (spapr->kernel_size &&
        vof_claim(spapr->fdt_blob, vof, spapr->kernel_addr, spapr->kernel_size,
                  0) == -1) {
        error_report("Memory for kernel is in use");
        exit(1);
    }

    if (spapr->initrd_size &&
        vof_claim(spapr->fdt_blob, vof, spapr->initrd_base, spapr->initrd_size,
                  0) == -1) {
        error_report("Memory for initramdisk is in use");
        exit(1);
    }

    /*
     * We skip writing FDT as nothing expects it; OF client interface is
     * going to be used for reading the device tree.
     */
}

target_ulong spapr_vof_client_architecture_support(CPUState *cs,
                                                  target_ulong ovec_addr)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(qdev_get_machine());

    return do_client_architecture_support(POWERPC_CPU(cs), spapr, ovec_addr,
                                          FDT_MAX_SIZE);
}

void spapr_vof_quiesce(void)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(qdev_get_machine());

    spapr->fdt_size = fdt_totalsize(spapr->fdt_blob);
    spapr->fdt_initial_size = spapr->fdt_size;
}
