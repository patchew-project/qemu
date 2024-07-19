#ifndef HW_RDT_H
#define HW_RDT_H

#include <stdbool.h>
#include <stdint.h>

/* RDT L3 Cache Monitoring Technology */
#define CPUID_15_0_EDX_L3               (1U << 1)
#define CPUID_15_1_EDX_L3_OCCUPANCY     (1U << 0)
#define CPUID_15_1_EDX_L3_TOTAL_BW      (1U << 1)
#define CPUID_15_1_EDX_L3_LOCAL_BW      (1U << 2)

/* RDT Cache Allocation Technology */
#define CPUID_10_0_EBX_L3_CAT           (1U << 1)
#define CPUID_10_0_EBX_L2_CAT           (1U << 2)
#define CPUID_10_0_EBX_MBA              (1U << 3)
#define CPUID_10_0_EDX CPUID_10_0_EBX_L3_CAT | CPUID_10_0_EBX_L2_CAT | CPUID_10_0_EBX_MBA

typedef struct RDTState RDTState;
typedef struct RDTStateInstance RDTStateInstance;
typedef struct RDTMonitor RDTMonitor;
typedef struct RDTAllocation RDTAllocation;

uint32_t rdt_get_cpuid_15_0_edx_l3(void);

uint32_t rdt_cpuid_15_1_edx_l3_total_bw_enabled(void);
uint32_t rdt_cpuid_15_1_edx_l3_local_bw_enabled(void);
uint32_t rdt_cpuid_15_1_edx_l3_occupancy_enabled(void);

uint32_t rdt_cpuid_10_0_ebx_l3_cat_enabled(void);
uint32_t rdt_cpuid_10_0_ebx_l2_cat_enabled(void);
uint32_t rdt_cpuid_10_0_ebx_l2_mba_enabled(void);

uint32_t rdt_get_cpuid_10_1_eax_cbm_length(void);
uint32_t rdt_cpuid_10_1_ebx_cbm_enabled(void);
uint32_t rdt_cpuid_10_1_ecx_cdp_enabled(void);
uint32_t rdt_get_cpuid_10_1_edx_cos_max(void);

uint32_t rdt_get_cpuid_10_2_eax_cbm_length(void);
uint32_t rdt_cpuid_10_2_ebx_cbm_enabled(void);
uint32_t rdt_get_cpuid_10_2_edx_cos_max(void);

uint32_t rdt_get_cpuid_10_3_eax_thrtl_max(void);
uint32_t rdt_cpuid_10_3_eax_linear_response_enabled(void);
uint32_t rdt_get_cpuid_10_3_edx_cos_max(void);

bool rdt_associate_rmid_cos(uint64_t msr_ia32_pqr_assoc);

void rdt_write_msr_l3_mask(uint32_t pos, uint32_t val);
void rdt_write_msr_l2_mask(uint32_t pos, uint32_t val);
void rdt_write_mba_thrtl(uint32_t pos, uint32_t val);

uint32_t rdt_read_l3_mask(uint32_t pos);
uint32_t rdt_read_l2_mask(uint32_t pos);
uint32_t rdt_read_mba_thrtl(uint32_t pos);

uint64_t rdt_read_event_count(RDTStateInstance *rdt, uint32_t rmid, uint32_t event_id);
uint32_t rdt_max_rmid(RDTStateInstance *rdt);

#endif
