#ifndef SYSEMU_NUMA_H
#define SYSEMU_NUMA_H

#include "qemu/bitmap.h"
#include "qapi/qapi-types-machine.h"
#include "exec/cpu-common.h"

struct CPUArchId;

#define MAX_NODES 128
#define NUMA_NODE_UNASSIGNED MAX_NODES
#define NUMA_DISTANCE_MIN         10
#define NUMA_DISTANCE_DEFAULT     20
#define NUMA_DISTANCE_MAX         254
#define NUMA_DISTANCE_UNREACHABLE 255

/* the value of AcpiHmatLBInfo flags */
enum {
    HMAT_LB_MEM_MEMORY           = 0,
    HMAT_LB_MEM_CACHE_1ST_LEVEL  = 1,
    HMAT_LB_MEM_CACHE_2ND_LEVEL  = 2,
    HMAT_LB_MEM_CACHE_3RD_LEVEL  = 3,
};

/* the value of AcpiHmatLBInfo data type */
enum {
    HMAT_LB_DATA_ACCESS_LATENCY   = 0,
    HMAT_LB_DATA_READ_LATENCY     = 1,
    HMAT_LB_DATA_WRITE_LATENCY    = 2,
    HMAT_LB_DATA_ACCESS_BANDWIDTH = 3,
    HMAT_LB_DATA_READ_BANDWIDTH   = 4,
    HMAT_LB_DATA_WRITE_BANDWIDTH  = 5,
};

#define UINT16_BITS       16

#define HMAT_LB_LEVELS    (HMAT_LB_MEM_CACHE_3RD_LEVEL + 1)
#define HMAT_LB_TYPES     (HMAT_LB_DATA_WRITE_BANDWIDTH + 1)

#define MAX_HMAT_CACHE_LEVEL    HMAT_LB_MEM_CACHE_3RD_LEVEL

struct NodeInfo {
    uint64_t node_mem;
    struct HostMemoryBackend *node_memdev;
    bool present;
    bool has_cpu;
    uint16_t initiator;
    uint8_t distance[MAX_NODES];
};

struct NumaNodeMem {
    uint64_t node_mem;
    uint64_t node_plugged_mem;
};

struct HMAT_LB_Data {
    uint8_t     initiator;
    uint8_t     target;
    uint64_t    rawdata;
};
typedef struct HMAT_LB_Data HMAT_LB_Data;

struct HMAT_LB_Info {
    /* Indicates it's memory or the specified level memory side cache. */
    uint8_t     hierarchy;

    /* Present the type of data, access/read/write latency or bandwidth. */
    uint8_t     data_type;

    /* The left range of latency for calculating common latency base */
    uint64_t    range_left_la;

    /* The range bitmap of bandwidth for calculating common bandwidth base */
    uint64_t    range_bitmap_bw;

    /* The common base unit for latencies */
    uint64_t    base_latency;

    /* The common base unit for bandwidths */
    uint64_t    base_bandwidth;

    /* Array to store the compressed latencies */
    uint16_t    *entry_latency;

    /* Array to store the compressed latencies */
    uint16_t    *entry_bandwidth;

    /* Array to store the latencies */
    GArray      *latency;

    /* Array to store the bandwidthes */
    GArray      *bandwidth;
};
typedef struct HMAT_LB_Info HMAT_LB_Info;

struct HMAT_Cache_Info {
    /* The memory proximity domain to which the memory belongs. */
    uint32_t    proximity;

    /* Size of memory side cache in bytes. */
    uint64_t    size;

    /* Total cache levels for this memory proximity domain. */
    uint8_t     total_levels;

    /* Cache level described in this structure. */
    uint8_t     level;

    /* Cache Associativity: None/Direct Mapped/Comple Cache Indexing */
    uint8_t     associativity;

    /* Write Policy: None/Write Back(WB)/Write Through(WT) */
    uint8_t     write_policy;

    /* Cache Line size in bytes. */
    uint16_t    line_size;
};
typedef struct HMAT_Cache_Info HMAT_Cache_Info;

struct NumaState {
    /* Number of NUMA nodes */
    int num_nodes;

    /* Allow setting NUMA distance for different NUMA nodes */
    bool have_numa_distance;

    /* Detect if HMAT support is enabled. */
    bool hmat_enabled;

    /* NUMA nodes information */
    NodeInfo nodes[MAX_NODES];

    /* NUMA nodes HMAT Locality Latency and Bandwidth Information */
    HMAT_LB_Info *hmat_lb[HMAT_LB_LEVELS][HMAT_LB_TYPES];

    /* Memory Side Cache Information Structure */
    HMAT_Cache_Info *hmat_cache[MAX_NODES][MAX_HMAT_CACHE_LEVEL + 1];
};
typedef struct NumaState NumaState;

void set_numa_options(MachineState *ms, NumaOptions *object, Error **errp);
void parse_numa_opts(MachineState *ms);
void parse_numa_hmat_lb(NumaState *numa_state, NumaHmatLBOptions *node,
                        Error **errp);
void parse_numa_hmat_cache(MachineState *ms, NumaHmatCacheOptions *node,
                           Error **errp);
void numa_complete_configuration(MachineState *ms);
void query_numa_node_mem(NumaNodeMem node_mem[], MachineState *ms);
extern QemuOptsList qemu_numa_opts;
void numa_legacy_auto_assign_ram(MachineClass *mc, NodeInfo *nodes,
                                 int nb_nodes, ram_addr_t size);
void numa_default_auto_assign_ram(MachineClass *mc, NodeInfo *nodes,
                                  int nb_nodes, ram_addr_t size);
void numa_cpu_pre_plug(const struct CPUArchId *slot, DeviceState *dev,
                       Error **errp);

#endif
