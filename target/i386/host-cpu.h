/*
 * x86 host CPU type initialization
 *
 * Copyright 2020 SUSE LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HOST_CPU_TYPE_H
#define HOST_CPU_TYPE_H

void host_cpu_type_init(void);

void host_cpu_initfn(Object *obj);
void host_cpu_realizefn(DeviceState *dev, Error **errp);
void host_cpu_max_initfn(X86CPU *cpu);

void host_cpu_vendor_fms(char *vendor, int *family, int *model, int *stepping);

#endif /* HOST_CPU_TYPE_H */
