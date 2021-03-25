/*
 * Virtual Open Firmware
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_VOF_H
#define HW_VOF_H

typedef struct Vof {
    uint32_t top_addr; /* copied from rma_size */
    GArray *claimed; /* array of SpaprOfClaimed */
    uint64_t claimed_base;
    GHashTable *of_instances; /* ihandle -> SpaprOfInstance */
    uint32_t of_instance_last;
    char *bootargs;
    uint32_t initrd_base; /* Updated in spapr at CAS */
    long initrd_size; /* Updated in spapr at CAS */
} Vof;

uint32_t vof_client_call(void *fdt, Vof *vof, const char *service,
                         uint32_t *args, unsigned nargs,
                         uint32_t *rets, unsigned nrets);
uint64_t vof_claim(void *fdt, Vof *vof, uint64_t virt, uint64_t size,
                   uint64_t align);
void vof_cleanup(Vof *vof);
void vof_build_dt(void *fdt, Vof *vof, uint32_t top_addr);
uint32_t vof_client_open_store(void *fdt, Vof *vof, const char *nodename,
                               const char *prop, const char *path);

#define TYPE_VOF_MACHINE_IF "vof-machine-if"

typedef struct VofMachineIfClass VofMachineIfClass;
DECLARE_CLASS_CHECKERS(VofMachineIfClass, VOF_MACHINE, TYPE_VOF_MACHINE_IF)

struct VofMachineIfClass {
    InterfaceClass parent;
    target_ulong (*client_architecture_support)(CPUState *cs, target_ulong vec);
    void (*quiesce)(void);
};

#endif /* HW_VOF_H */
