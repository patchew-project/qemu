/*
 * Device descriptions for Intel platforms.
 *
 * Copyright Intel Coporation 2017
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include "intel-platform.h"

#define    SNB_GMCH_GGMS_SHIFT  8 /* GTT Graphics Memory Size */
#define    SNB_GMCH_GGMS_MASK   0x3
#define    SNB_GMCH_GMS_SHIFT   3 /* Graphics Mode Select */
#define    SNB_GMCH_GMS_MASK    0x1f
#define    BDW_GMCH_GGMS_SHIFT  6
#define    BDW_GMCH_GGMS_MASK   0x3
#define    BDW_GMCH_GMS_SHIFT   8
#define    BDW_GMCH_GMS_MASK    0xff

static unsigned int gen6_get_stolen_size(uint16_t gmch)
{
    gmch >>= SNB_GMCH_GMS_SHIFT;
    gmch &= SNB_GMCH_GMS_MASK;
    return gmch << 25; /* 32 MB units */
}

static unsigned int bdw_get_stolen_size(uint16_t gmch)
{
    gmch >>= BDW_GMCH_GMS_SHIFT;
    gmch &= BDW_GMCH_GMS_MASK;
    return gmch << 25; /* 32 MB units */
}

static unsigned int chv_get_stolen_size(uint16_t gmch)
{
    gmch >>= SNB_GMCH_GMS_SHIFT;
    gmch &= SNB_GMCH_GMS_MASK;

    /*
     * 0x0  to 0x10: 32MB increments starting at 0MB
     * 0x11 to 0x16: 4MB increments starting at 8MB
     * 0x17 to 0x1d: 4MB increments start at 36MB
     */
    if (gmch < 0x11)
        return gmch << 25;
    else if (gmch < 0x17)
        return (gmch - 0x11 + 2) << 22;
    else
        return (gmch - 0x17 + 9) << 22;
}

static unsigned int gen9_get_stolen_size(uint16_t gmch)
{
    gmch >>= BDW_GMCH_GMS_SHIFT;
    gmch &= BDW_GMCH_GMS_MASK;

    if (gmch < 0xf0)
        return gmch << 25; /* 32 MB units */
    else
        /* 4MB increments starting at 0xf0 for 4MB */
        return (gmch - 0xf0 + 1) << 22;
}

static unsigned int gen6_get_gtt_size(uint16_t gmch)
{
        gmch >>= SNB_GMCH_GGMS_SHIFT;
        gmch &= SNB_GMCH_GGMS_MASK;
        return gmch << 20;
}

static unsigned int gen8_get_gtt_size(uint16_t gmch)
{
        gmch >>= BDW_GMCH_GGMS_SHIFT;
        gmch &= BDW_GMCH_GGMS_MASK;
        if (gmch)
                gmch = 1 << gmch;

        return gmch << 20;
}

static unsigned int chv_get_gtt_size(uint16_t gmch)
{
        gmch >>= SNB_GMCH_GGMS_SHIFT;
        gmch &= SNB_GMCH_GGMS_MASK;

        if (gmch)
                return 1 << (20 + gmch);

        return 0;
}

static const struct intel_device_info intel_sandybridge_info = {
    .gen = 6,
    .platform = INTEL_SANDYBRIDGE,
    .gtt_entry_size = 4,
    .get_stolen_size = gen6_get_stolen_size,
    .get_gtt_size = gen6_get_gtt_size,
};

static const struct intel_device_info intel_ivybridge_info = {
    .gen = 7,
    .platform = INTEL_IVYBRIDGE,
    .gtt_entry_size = 4,
    .get_stolen_size = gen6_get_stolen_size,
    .get_gtt_size = gen6_get_gtt_size,
};

static const struct intel_device_info intel_valleyview_info = {
    .gen = 7,
    .platform = INTEL_VALLEYVIEW,
    .gtt_entry_size = 4,
    .get_stolen_size = gen6_get_stolen_size,
    .get_gtt_size = gen6_get_gtt_size,
};

static const struct intel_device_info intel_haswell_info = {
    .gen = 7,   /* Actually HASWELL is GEN 7.5 */
    .platform = INTEL_HASWELL,
    .gtt_entry_size = 4,
    .get_stolen_size = gen6_get_stolen_size,
    .get_gtt_size = gen6_get_gtt_size,
};

static const struct intel_device_info intel_broadwell_info = {
    .gen = 8,
    .platform = INTEL_BROADWELL,
    .gtt_entry_size = 8,
    .get_stolen_size = bdw_get_stolen_size,
    .get_gtt_size = gen8_get_gtt_size,
};

static const struct intel_device_info intel_cherryview_info = {
    .gen = 8,
    .platform = INTEL_CHERRYVIEW,
    .gtt_entry_size = 8,
    .get_stolen_size = chv_get_stolen_size,
    .get_gtt_size = chv_get_gtt_size,
};

static const struct intel_device_info intel_skylake_info = {
    .gen = 9,
    .platform = INTEL_SKYLAKE,
    .gtt_entry_size = 8,
    .get_stolen_size = gen9_get_stolen_size,
    .get_gtt_size = gen8_get_gtt_size,
};

static const struct intel_device_info intel_broxton_info = {
    .gen = 9,
    .platform = INTEL_BROXTON,
    .gtt_entry_size = 8,
    .get_stolen_size = gen9_get_stolen_size,
    .get_gtt_size = gen8_get_gtt_size,
};

struct intel_pci_device_id {
    uint16_t device_id;
    const struct intel_device_info *info;
};

#define INTEL_VGA_DEVICE(id, info) \
    { id, info }

#define INTEL_SNB_D_IDS(info) \
    INTEL_VGA_DEVICE(0x0102, info), \
    INTEL_VGA_DEVICE(0x0112, info), \
    INTEL_VGA_DEVICE(0x0122, info), \
    INTEL_VGA_DEVICE(0x010A, info)

#define INTEL_SNB_M_IDS(info) \
    INTEL_VGA_DEVICE(0x0106, info), \
    INTEL_VGA_DEVICE(0x0116, info), \
    INTEL_VGA_DEVICE(0x0126, info)

#define INTEL_IVB_M_IDS(info) \
    INTEL_VGA_DEVICE(0x0156, info), /* GT1 mobile */ \
    INTEL_VGA_DEVICE(0x0166, info)  /* GT2 mobile */

#define INTEL_IVB_D_IDS(info) \
    INTEL_VGA_DEVICE(0x0152, info), /* GT1 desktop */ \
    INTEL_VGA_DEVICE(0x0162, info), /* GT2 desktop */ \
    INTEL_VGA_DEVICE(0x015a, info), /* GT1 server */ \
    INTEL_VGA_DEVICE(0x016a, info)  /* GT2 server */

#define INTEL_HSW_IDS(info) \
    INTEL_VGA_DEVICE(0x0402, info), /* GT1 desktop */ \
    INTEL_VGA_DEVICE(0x0412, info), /* GT2 desktop */ \
    INTEL_VGA_DEVICE(0x0422, info), /* GT3 desktop */ \
    INTEL_VGA_DEVICE(0x040a, info), /* GT1 server */ \
    INTEL_VGA_DEVICE(0x041a, info), /* GT2 server */ \
    INTEL_VGA_DEVICE(0x042a, info), /* GT3 server */ \
    INTEL_VGA_DEVICE(0x040B, info), /* GT1 reserved */ \
    INTEL_VGA_DEVICE(0x041B, info), /* GT2 reserved */ \
    INTEL_VGA_DEVICE(0x042B, info), /* GT3 reserved */ \
    INTEL_VGA_DEVICE(0x040E, info), /* GT1 reserved */ \
    INTEL_VGA_DEVICE(0x041E, info), /* GT2 reserved */ \
    INTEL_VGA_DEVICE(0x042E, info), /* GT3 reserved */ \
    INTEL_VGA_DEVICE(0x0C02, info), /* SDV GT1 desktop */ \
    INTEL_VGA_DEVICE(0x0C12, info), /* SDV GT2 desktop */ \
    INTEL_VGA_DEVICE(0x0C22, info), /* SDV GT3 desktop */ \
    INTEL_VGA_DEVICE(0x0C0A, info), /* SDV GT1 server */ \
    INTEL_VGA_DEVICE(0x0C1A, info), /* SDV GT2 server */ \
    INTEL_VGA_DEVICE(0x0C2A, info), /* SDV GT3 server */ \
    INTEL_VGA_DEVICE(0x0C0B, info), /* SDV GT1 reserved */ \
    INTEL_VGA_DEVICE(0x0C1B, info), /* SDV GT2 reserved */ \
    INTEL_VGA_DEVICE(0x0C2B, info), /* SDV GT3 reserved */ \
    INTEL_VGA_DEVICE(0x0C0E, info), /* SDV GT1 reserved */ \
    INTEL_VGA_DEVICE(0x0C1E, info), /* SDV GT2 reserved */ \
    INTEL_VGA_DEVICE(0x0C2E, info), /* SDV GT3 reserved */ \
    INTEL_VGA_DEVICE(0x0A02, info), /* ULT GT1 desktop */ \
    INTEL_VGA_DEVICE(0x0A12, info), /* ULT GT2 desktop */ \
    INTEL_VGA_DEVICE(0x0A22, info), /* ULT GT3 desktop */ \
    INTEL_VGA_DEVICE(0x0A0A, info), /* ULT GT1 server */ \
    INTEL_VGA_DEVICE(0x0A1A, info), /* ULT GT2 server */ \
    INTEL_VGA_DEVICE(0x0A2A, info), /* ULT GT3 server */ \
    INTEL_VGA_DEVICE(0x0A0B, info), /* ULT GT1 reserved */ \
    INTEL_VGA_DEVICE(0x0A1B, info), /* ULT GT2 reserved */ \
    INTEL_VGA_DEVICE(0x0A2B, info), /* ULT GT3 reserved */ \
    INTEL_VGA_DEVICE(0x0D02, info), /* CRW GT1 desktop */ \
    INTEL_VGA_DEVICE(0x0D12, info), /* CRW GT2 desktop */ \
    INTEL_VGA_DEVICE(0x0D22, info), /* CRW GT3 desktop */ \
    INTEL_VGA_DEVICE(0x0D0A, info), /* CRW GT1 server */ \
    INTEL_VGA_DEVICE(0x0D1A, info), /* CRW GT2 server */ \
    INTEL_VGA_DEVICE(0x0D2A, info), /* CRW GT3 server */ \
    INTEL_VGA_DEVICE(0x0D0B, info), /* CRW GT1 reserved */ \
    INTEL_VGA_DEVICE(0x0D1B, info), /* CRW GT2 reserved */ \
    INTEL_VGA_DEVICE(0x0D2B, info), /* CRW GT3 reserved */ \
    INTEL_VGA_DEVICE(0x0D0E, info), /* CRW GT1 reserved */ \
    INTEL_VGA_DEVICE(0x0D1E, info), /* CRW GT2 reserved */ \
    INTEL_VGA_DEVICE(0x0D2E, info),  /* CRW GT3 reserved */ \
    INTEL_VGA_DEVICE(0x0406, info), /* GT1 mobile */ \
    INTEL_VGA_DEVICE(0x0416, info), /* GT2 mobile */ \
    INTEL_VGA_DEVICE(0x0426, info), /* GT2 mobile */ \
    INTEL_VGA_DEVICE(0x0C06, info), /* SDV GT1 mobile */ \
    INTEL_VGA_DEVICE(0x0C16, info), /* SDV GT2 mobile */ \
    INTEL_VGA_DEVICE(0x0C26, info), /* SDV GT3 mobile */ \
    INTEL_VGA_DEVICE(0x0A06, info), /* ULT GT1 mobile */ \
    INTEL_VGA_DEVICE(0x0A16, info), /* ULT GT2 mobile */ \
    INTEL_VGA_DEVICE(0x0A26, info), /* ULT GT3 mobile */ \
    INTEL_VGA_DEVICE(0x0A0E, info), /* ULX GT1 mobile */ \
    INTEL_VGA_DEVICE(0x0A1E, info), /* ULX GT2 mobile */ \
    INTEL_VGA_DEVICE(0x0A2E, info), /* ULT GT3 reserved */ \
    INTEL_VGA_DEVICE(0x0D06, info), /* CRW GT1 mobile */ \
    INTEL_VGA_DEVICE(0x0D16, info), /* CRW GT2 mobile */ \
    INTEL_VGA_DEVICE(0x0D26, info)  /* CRW GT3 mobile */

#define INTEL_VLV_IDS(info) \
    INTEL_VGA_DEVICE(0x0f30, info), \
    INTEL_VGA_DEVICE(0x0f31, info), \
    INTEL_VGA_DEVICE(0x0f32, info), \
    INTEL_VGA_DEVICE(0x0f33, info), \
    INTEL_VGA_DEVICE(0x0157, info), \
    INTEL_VGA_DEVICE(0x0155, info)

#define INTEL_BDW_GT12_IDS(info)  \
    INTEL_VGA_DEVICE(0x1602, info), /* GT1 ULT */ \
    INTEL_VGA_DEVICE(0x1606, info), /* GT1 ULT */ \
    INTEL_VGA_DEVICE(0x160B, info), /* GT1 Iris */ \
    INTEL_VGA_DEVICE(0x160E, info), /* GT1 ULX */ \
    INTEL_VGA_DEVICE(0x1612, info), /* GT2 Halo */ \
    INTEL_VGA_DEVICE(0x1616, info), /* GT2 ULT */ \
    INTEL_VGA_DEVICE(0x161B, info), /* GT2 ULT */ \
    INTEL_VGA_DEVICE(0x161E, info),  /* GT2 ULX */ \
    INTEL_VGA_DEVICE(0x160A, info), /* GT1 Server */ \
    INTEL_VGA_DEVICE(0x160D, info), /* GT1 Workstation */ \
    INTEL_VGA_DEVICE(0x161A, info), /* GT2 Server */ \
    INTEL_VGA_DEVICE(0x161D, info)  /* GT2 Workstation */

#define INTEL_BDW_GT3_IDS(info) \
    INTEL_VGA_DEVICE(0x1622, info), /* ULT */ \
    INTEL_VGA_DEVICE(0x1626, info), /* ULT */ \
    INTEL_VGA_DEVICE(0x162B, info), /* Iris */ \
    INTEL_VGA_DEVICE(0x162E, info),  /* ULX */\
    INTEL_VGA_DEVICE(0x162A, info), /* Server */ \
    INTEL_VGA_DEVICE(0x162D, info)  /* Workstation */

#define INTEL_BDW_RSVD_IDS(info) \
    INTEL_VGA_DEVICE(0x1632, info), /* ULT */ \
    INTEL_VGA_DEVICE(0x1636, info), /* ULT */ \
    INTEL_VGA_DEVICE(0x163B, info), /* Iris */ \
    INTEL_VGA_DEVICE(0x163E, info), /* ULX */ \
    INTEL_VGA_DEVICE(0x163A, info), /* Server */ \
    INTEL_VGA_DEVICE(0x163D, info)  /* Workstation */

#define INTEL_BDW_IDS(info) \
    INTEL_BDW_GT12_IDS(info), \
    INTEL_BDW_GT3_IDS(info), \
    INTEL_BDW_RSVD_IDS(info)

#define INTEL_CHV_IDS(info) \
    INTEL_VGA_DEVICE(0x22b0, info), \
    INTEL_VGA_DEVICE(0x22b1, info), \
    INTEL_VGA_DEVICE(0x22b2, info), \
    INTEL_VGA_DEVICE(0x22b3, info)

#define INTEL_SKL_GT1_IDS(info)	\
    INTEL_VGA_DEVICE(0x1906, info), /* ULT GT1 */ \
    INTEL_VGA_DEVICE(0x190E, info), /* ULX GT1 */ \
    INTEL_VGA_DEVICE(0x1902, info), /* DT  GT1 */ \
    INTEL_VGA_DEVICE(0x190B, info), /* Halo GT1 */ \
    INTEL_VGA_DEVICE(0x190A, info) /* SRV GT1 */

#define INTEL_SKL_GT2_IDS(info)	\
    INTEL_VGA_DEVICE(0x1916, info), /* ULT GT2 */ \
    INTEL_VGA_DEVICE(0x1921, info), /* ULT GT2F */ \
    INTEL_VGA_DEVICE(0x191E, info), /* ULX GT2 */ \
    INTEL_VGA_DEVICE(0x1912, info), /* DT  GT2 */ \
    INTEL_VGA_DEVICE(0x191B, info), /* Halo GT2 */ \
    INTEL_VGA_DEVICE(0x191A, info), /* SRV GT2 */ \
    INTEL_VGA_DEVICE(0x191D, info)  /* WKS GT2 */

#define INTEL_SKL_GT3_IDS(info) \
    INTEL_VGA_DEVICE(0x1923, info), /* ULT GT3 */ \
    INTEL_VGA_DEVICE(0x1926, info), /* ULT GT3 */ \
    INTEL_VGA_DEVICE(0x1927, info), /* ULT GT3 */ \
    INTEL_VGA_DEVICE(0x192B, info)  /* Halo GT3 */ \

#define INTEL_SKL_GT4_IDS(info) \
    INTEL_VGA_DEVICE(0x1932, info), /* DT GT4 */ \
    INTEL_VGA_DEVICE(0x193B, info), /* Halo GT4 */ \
    INTEL_VGA_DEVICE(0x193D, info), /* WKS GT4 */ \
    INTEL_VGA_DEVICE(0x192A, info), /* SRV GT4 */ \
    INTEL_VGA_DEVICE(0x193A, info)  /* SRV GT4e */

#define INTEL_SKL_IDS(info)	 \
    INTEL_SKL_GT1_IDS(info), \
    INTEL_SKL_GT2_IDS(info), \
    INTEL_SKL_GT3_IDS(info), \
    INTEL_SKL_GT4_IDS(info)

#define INTEL_BXT_IDS(info) \
    INTEL_VGA_DEVICE(0x0A84, info), \
    INTEL_VGA_DEVICE(0x1A84, info), \
    INTEL_VGA_DEVICE(0x1A85, info), \
    INTEL_VGA_DEVICE(0x5A84, info), /* APL HD Graphics 505 */ \
    INTEL_VGA_DEVICE(0x5A85, info)  /* APL HD Graphics 500 */

static const struct intel_pci_device_id pciidlist[] = {
    INTEL_SNB_D_IDS(&intel_sandybridge_info),
    INTEL_SNB_M_IDS(&intel_sandybridge_info),
    INTEL_IVB_M_IDS(&intel_ivybridge_info),
    INTEL_IVB_D_IDS(&intel_ivybridge_info),
    INTEL_HSW_IDS(&intel_haswell_info),
    INTEL_VLV_IDS(&intel_valleyview_info),
    INTEL_BDW_GT12_IDS(&intel_broadwell_info),
    INTEL_BDW_GT3_IDS(&intel_broadwell_info),
    INTEL_BDW_RSVD_IDS(&intel_broadwell_info),
    INTEL_CHV_IDS(&intel_cherryview_info),
    INTEL_SKL_GT1_IDS(&intel_skylake_info),
    INTEL_SKL_GT2_IDS(&intel_skylake_info),
    INTEL_SKL_GT3_IDS(&intel_skylake_info),
    INTEL_SKL_GT4_IDS(&intel_skylake_info),
    INTEL_BXT_IDS(&intel_broxton_info),
};

const struct intel_device_info *intel_get_device_info(uint16_t device_id)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(pciidlist); i++)
        if (pciidlist[i].device_id == device_id)
            return pciidlist[i].info;

    return NULL;
}
