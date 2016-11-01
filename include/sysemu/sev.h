/*
 * QEMU Secure Encrypted Virutualization (SEV) support
 *
 * Copyright: Advanced Micro Devices, 2016
 *
 * Authors:
 *  Brijesh Singh <brijesh.singh@amd.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_SEV_H
#define QEMU_SEV_H

#include <linux/kvm.h>

#include "qom/object.h"
#include "qapi/error.h"
#include "sysemu/kvm.h"

#define TYPE_QSEV_LAUNCH_INFO "sev-launch-info"
#define QSEV_LAUNCH_INFO(obj)                  \
    OBJECT_CHECK(QSevLaunchInfo, (obj), TYPE_QSEV_LAUNCH_INFO)

typedef struct QSevLaunchInfo QSevLaunchInfo;
typedef struct QSevLaunchInfoClass QSevLaunchInfoClass;

/**
 * QSevLaunchInfo:
 *
 * The QSevLaunchInfo object provides parameters to launch a SEV
 * guest from unnencrypted boot images. SEV will encrypt the boot images using
 * guest owner's key before launching the guest.
 *
 * # $QEMU -object sev-launch-info,id=launch0,dh-pub-qx=abcd \
 *         ....
 */
struct QSevLaunchInfo {
    Object parent_obj;
    char *nonce;
    char *dh_pub_qx;
    char *dh_pub_qy;
};

struct QSevLaunchInfoClass {
    ObjectClass parent_class;
};

#define TYPE_QSEV_GUEST_INFO "sev-guest"
#define QSEV_GUEST_INFO(obj)                  \
    OBJECT_CHECK(QSevGuestInfo, (obj), TYPE_QSEV_GUEST_INFO)

typedef struct QSevGuestInfo QSevGuestInfo;
typedef struct QSevGuestInfoClass QSevGuestInfoClass;

/**
 * QSevGuestInfo:
 *
 * The QSevGuestInfo object is used for creating a SEV guest.
 *
 * e.g to launch a SEV guest from unencrypted boot images
 *
 * # $QEMU -object sev-launch-info,id=launch0 \
 *         -object sev-guest,id=sev0 \
 *         -object security-policy,id=secure0,memory-encryption=sev0 \
 *         -machine ...security-policy=secure0
 */
struct QSevGuestInfo {
    Object parent_obj;

    QSevLaunchInfo *launch_info;
};

struct QSevGuestInfoClass {
    ObjectClass parent_class;
};

struct SEVState {
    uint8_t state;
    QSevGuestInfo *sev_info;
};

typedef struct SEVState SEVState;

enum {
    INVALID_TYPE = 0,
    USE_LAUNCH_INFO,
    USE_RECEIVE_INFO
};

enum {
    SEV_STATE_INVALID = 0,
    SEV_STATE_LAUNCHING,
    SEV_STATE_RECEIVING,
    SEV_STATE_SENDING,
    SEV_STATE_RUNNING
};

bool sev_enabled(void);
void *sev_guest_init(const char *keyid);
int sev_guest_launch_start(void *handle);
int sev_guest_launch_finish(void *handle);
void sev_guest_set_debug_ops(void *handle, MemoryRegion *mr);
int sev_guest_mem_dec(void *handle, uint8_t *dst,
        const uint8_t *src, uint32_t len);
int sev_guest_mem_enc(void *handle, uint8_t *dst,
        const uint8_t *src, uint32_t len);

#endif

