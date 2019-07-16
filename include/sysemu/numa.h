#ifndef SYSEMU_NUMA_H
#define SYSEMU_NUMA_H

#include "qemu/bitmap.h"
#include "sysemu/sysemu.h"
#include "sysemu/hostmem.h"
#include "hw/boards.h"

struct NodeInfo {
    uint64_t node_mem;
    struct HostMemoryBackend *node_memdev;
    bool present;
    bool has_cpu;
    bool initiator_valid;
    uint16_t initiator;
    uint8_t distance[MAX_NODES];
};

struct NumaNodeMem {
    uint64_t node_mem;
    uint64_t node_plugged_mem;
};

struct NumaState {
    /* Number of NUMA nodes */
    int num_nodes;

    /* Allow setting NUMA distance for different NUMA nodes */
    bool have_numa_distance;

    /* NUMA nodes information */
    NodeInfo nodes[MAX_NODES];

    /* NUMA modes HMAT Locality Latency and Bandwidth Information */
    HMAT_LB_Info *hmat_lb[HMAT_LB_LEVELS][HMAT_LB_TYPES];

    /* Memory Side Cache Information Structure */
    HMAT_Cache_Info *hmat_cache[MAX_NODES][MAX_HMAT_CACHE_LEVEL + 1];
};
typedef struct NumaState NumaState;

void set_numa_options(MachineState *ms, NumaOptions *object, Error **errp);
void parse_numa_opts(MachineState *ms);
void parse_numa_hmat_lb(MachineState *ms, NumaHmatLBOptions *node,
                        Error **errp);
void numa_complete_configuration(MachineState *ms);
void query_numa_node_mem(NumaNodeMem node_mem[], MachineState *ms);
extern QemuOptsList qemu_numa_opts;
void numa_legacy_auto_assign_ram(MachineClass *mc, NodeInfo *nodes,
                                 int nb_nodes, ram_addr_t size);
void numa_default_auto_assign_ram(MachineClass *mc, NodeInfo *nodes,
                                  int nb_nodes, ram_addr_t size);
void numa_cpu_pre_plug(const CPUArchId *slot, DeviceState *dev, Error **errp);
#endif
