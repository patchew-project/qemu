/*
 * QEMU S390 IPL Block
 *
 * Copyright 2015 IBM Corp.
 * Author(s): Alexander Yarygin <yarygin@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef IPLB_H
#define IPLB_H

#ifndef QEMU_PACKED
#define QEMU_PACKED __attribute__((packed))
#endif

#include <qipl.h>
#include <string.h>

extern QemuIplParameters qipl;
extern IplParameterBlock *iplb;
extern bool have_iplb;

struct IplInfoReportBlockHeader {
    uint32_t len;
    uint8_t  iirb_flags;
    uint8_t  reserved1[2];
    uint8_t  version;
    uint8_t  reserved2[8];
} __attribute__ ((packed));
typedef struct IplInfoReportBlockHeader IplInfoReportBlockHeader;

struct IplInfoBlockHeader {
    uint32_t len;
    uint8_t  ibt;
    uint8_t  reserved1[3];
    uint8_t  reserved2[8];
} __attribute__ ((packed));
typedef struct IplInfoBlockHeader IplInfoBlockHeader;

enum IplIbt {
    IPL_IBT_CERTIFICATES = 1,
    IPL_IBT_COMPONENTS = 2,
};

struct IplSignatureCertificateEntry {
    uint64_t addr;
    uint64_t len;
} __attribute__ ((packed));
typedef struct IplSignatureCertificateEntry IplSignatureCertificateEntry;

struct IplSignatureCertificateList {
    IplInfoBlockHeader            ipl_info_header;
    IplSignatureCertificateEntry  cert_entries[MAX_CERTIFICATES];
} __attribute__ ((packed));
typedef struct IplSignatureCertificateList IplSignatureCertificateList;

#define S390_IPL_COMPONENT_FLAG_SC  0x80
#define S390_IPL_COMPONENT_FLAG_CSV 0x40

struct IplDeviceComponentEntry {
    uint64_t addr;
    uint64_t len;
    uint8_t  flags;
    uint8_t  reserved1[5];
    uint16_t cert_index;
    uint8_t  reserved2[8];
} __attribute__ ((packed));
typedef struct IplDeviceComponentEntry IplDeviceComponentEntry;

struct IplDeviceComponentList {
    IplInfoBlockHeader       ipl_info_header;
    IplDeviceComponentEntry  device_entries[MAX_CERTIFICATES];
} __attribute__ ((packed));
typedef struct IplDeviceComponentList IplDeviceComponentList;

#define COMP_LIST_MAX   sizeof(IplDeviceComponentList)
#define CERT_LIST_MAX   sizeof(IplSignatureCertificateList)

struct IplInfoReportBlock {
    IplInfoReportBlockHeader     hdr;
    uint8_t                      info_blks[COMP_LIST_MAX + CERT_LIST_MAX];
} __attribute__ ((packed));
typedef struct IplInfoReportBlock IplInfoReportBlock;

struct IplBlocks {
    IplParameterBlock   iplb;
    IplInfoReportBlock  iirb;
} __attribute__ ((packed));
typedef struct IplBlocks IplBlocks;

extern IplBlocks ipl_data __attribute__((__aligned__(PAGE_SIZE)));

#define S390_IPL_TYPE_FCP 0x00
#define S390_IPL_TYPE_CCW 0x02
#define S390_IPL_TYPE_QEMU_SCSI 0xff

static inline bool manage_iplb(IplParameterBlock *iplb, bool store)
{
    register unsigned long addr asm("0") = (unsigned long) iplb;
    register unsigned long rc asm("1") = 0;
    unsigned long subcode = store ? 6 : 5;

    asm volatile ("diag %0,%2,0x308\n"
                  : "+d" (addr), "+d" (rc)
                  : "d" (subcode)
                  : "memory", "cc");
    return rc == 0x01;
}


static inline bool store_iplb(IplParameterBlock *iplb)
{
    return manage_iplb(iplb, true);
}

static inline bool set_iplb(IplParameterBlock *iplb)
{
    return manage_iplb(iplb, false);
}

/*
 * The IPL started on the device, but failed in some way.  If the IPLB chain
 * still has more devices left to try, use the next device in order.
 */
static inline bool load_next_iplb(void)
{
    IplParameterBlock *next_iplb;

    if (qipl.chain_len < 1) {
        return false;
    }

    qipl.index++;
    next_iplb = (IplParameterBlock *) qipl.next_iplb;
    memcpy(iplb, next_iplb, sizeof(IplParameterBlock));

    qipl.chain_len--;
    qipl.next_iplb = qipl.next_iplb + sizeof(IplParameterBlock);

    return true;
}

#endif /* IPLB_H */
