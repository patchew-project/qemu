/*
 * QEMU Secure Encrypted Virutualization (SEV) support
 *
 * Copyright: Advanced Micro Devices, 2016-2018
 *
 * Authors:
 *  Brijesh Singh <brijesh.singh@amd.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_SEV_I386_H
#define QEMU_SEV_I386_H

#include "qom/object.h"
#include "qapi/error.h"
#include "sysemu/kvm.h"
#include "sysemu/sev.h"
#include "qemu/error-report.h"
#include "qemu/uuid.h"
#include "qapi/qapi-types-misc-target.h"

#define SEV_POLICY_NODBG        0x1
#define SEV_POLICY_NOKS         0x2
#define SEV_POLICY_ES           0x4
#define SEV_POLICY_NOSEND       0x8
#define SEV_POLICY_DOMAIN       0x10
#define SEV_POLICY_SEV          0x20

#define SEV_ROM_SECRET_GUID "adf956ad-e98c-484c-ae11-b51c7d336447"

#define TYPE_QSEV_GUEST_INFO "sev-guest"
#define QSEV_GUEST_INFO(obj)                  \
    OBJECT_CHECK(QSevGuestInfo, (obj), TYPE_QSEV_GUEST_INFO)

extern bool sev_enabled(void);
extern uint64_t sev_get_me_mask(void);
extern SevInfo *sev_get_info(void);
extern uint32_t sev_get_cbit_position(void);
extern uint32_t sev_get_reduced_phys_bits(void);
extern char *sev_get_launch_measurement(void);
extern SevCapability *sev_get_capabilities(void);

typedef struct QSevGuestInfo QSevGuestInfo;
typedef struct QSevGuestInfoClass QSevGuestInfoClass;
typedef struct SevROMSecretTable SevROMSecretTable;

/**
 * If guest physical address for the launch secret is
 * provided in the ROM, it should be in the following
 * page-aligned structure.
 */
struct SevROMSecretTable {
    QemuUUID guid;
    unsigned int base;
    unsigned int size;
};

/**
 * QSevGuestInfo:
 *
 * The QSevGuestInfo object is used for creating a SEV guest.
 *
 * # $QEMU \
 *         -object sev-guest,id=sev0 \
 *         -machine ...,memory-encryption=sev0
 */
struct QSevGuestInfo {
    Object parent_obj;

    char *sev_device;
    uint32_t policy;
    uint32_t handle;
    char *dh_cert_file;
    char *session_file;
    uint32_t cbitpos;
    uint32_t reduced_phys_bits;
};

struct QSevGuestInfoClass {
    ObjectClass parent_class;
};

struct SEVState {
    QSevGuestInfo *sev_info;
    uint8_t api_major;
    uint8_t api_minor;
    uint8_t build_id;
    uint32_t policy;
    uint64_t me_mask;
    uint32_t cbitpos;
    uint32_t reduced_phys_bits;
    uint32_t handle;
    uint64_t secret_gpa;
    int sev_fd;
    SevState state;
    gchar *measurement;
};

typedef struct SEVState SEVState;

#endif
