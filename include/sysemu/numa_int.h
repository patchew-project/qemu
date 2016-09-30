#ifndef SYSEMU_NUMA_INT_H
#define SYSEMU_NUMA_INT_H

#include "sysemu/numa.h"

extern int have_memdevs;
extern int max_numa_nodeid;

int parse_numa(void *opaque, QemuOpts *opts, Error **errp);

#endif
