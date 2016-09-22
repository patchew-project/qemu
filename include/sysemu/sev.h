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

#define TYPE_QSEV_GUEST_INFO "sev-guest-info"
#define QSEV_GUEST_INFO(obj)                  \
    OBJECT_CHECK(QSevGuestInfo, (obj), TYPE_QSEV_GUEST_INFO)

typedef struct QSevGuestInfo QSevGuestInfo;
typedef struct QSevGuestInfoClass QSevGuestInfoClass;

/**
 * QSevGuestInfo:
 *
 * The QSevGuestInfo object provides the guest launch and migration ID
 * when memory encryption support is enabled in security-policy.
 *
 * The QSevGuestInfo object provides two properties:
 * - launch: should be set to SEV guest launch object ID
 * - send: should be set to SEV guest send object ID
 *
 * e.g to launch a unencrypted SEV guest
 *
 * # $QEMU -object sev-launch-info,id=launch0,flags.ks=off \
 *         -object sev-migrate-send,id=send0 \
 *         -object sev-guest-info,id=sev0,launch=launch0 send=send0 \
 *         -object security-policy,id=secure0,memory-encryption=sev0 \
 *         -machine ...security-policy=secure0
 */
struct QSevGuestInfo {
    Object parent_obj;

    char *launch;
    char *send;
};

struct QSevGuestInfoClass {
    ObjectClass parent_class;
};

#define TYPE_QSEV_POLICY_INFO "sev-policy-info"
#define QSEV_POLICY_INFO(obj)                  \
    OBJECT_CHECK(QSevPolicyInfo, (obj), TYPE_QSEV_POLICY_INFO)

typedef struct QSevPolicyInfo QSevPolicyInfo;
typedef struct QSevPolicyInfoClass QSevPolicyInfoClass;

/**
 * QSevPolicyInfo:
 *
 * The QSevPolicyInfo object provides the SEV guest policy parameters used
 * in launch and send commands.
 *
 * # $QEMU -object sev-policy-info,id=policy0,debug=on,ks=on,nosend=off \
 *         -object sev-launch-info,id=launch0,flag.ks=on,policy=policy0\
 *         ....
 */
struct QSevPolicyInfo {
    Object parent_obj;
    bool debug;
    bool ks;
    bool nosend;
    bool domain;
    bool sev;
    uint8_t fw_major;
    uint8_t fw_minor;
};

struct QSevPolicyInfoClass {
    ObjectClass parent_class;
};

#define TYPE_QSEV_LAUNCH_INFO "sev-launch-info"
#define QSEV_LAUNCH_INFO(obj)                  \
    OBJECT_CHECK(QSevLaunchInfo, (obj), TYPE_QSEV_LAUNCH_INFO)

typedef struct QSevLaunchInfo QSevLaunchInfo;
typedef struct QSevLaunchInfoClass QSevLaunchInfoClass;

/**
 * QSevLaunchInfo:
 *
 * The QSevLaunchInfo object provides parameters to launch an unencrypted
 * sev guest. A unencrypted guest launch means that the guest owners
 * provided OS images (kernel, initrd and bios) are unencrypted and SEV
 * would encrypt the images using guest owner's key creating using the
 * launch parameters.
 *
 * # $QEMU -object sev-policy,debug=on,ks=on,nosend=off,id=policy1 \
 *         -object sev-launch-info,flag.ks=on,policy=policy1,id=sev \
 *         -object sev-guest-info,launch=sev,id=secure-guest \
 *         ....
 */
struct QSevLaunchInfo {
    Object parent_obj;
    uint32_t handle;
    bool flags_ks;
    char *policy_id;
    uint8_t nonce[16];
    uint8_t dh_pub_qx[32];
    uint8_t dh_pub_qy[32];
};

struct QSevLaunchInfoClass {
    ObjectClass parent_class;
};

#define TYPE_QSEV_RECEIVE_INFO "sev-receive-info"
#define QSEV_RECEIVE_INFO(obj)                  \
    OBJECT_CHECK(QSevReceiveInfo, (obj), TYPE_QSEV_RECEIVE_INFO)

typedef struct QSevReceiveInfo QSevReceiveInfo;
typedef struct QSevReceiveInfoClass QSevReceiveInfoClass;

/**
 * QSevReceiveInfo:
 *
 * The QSevReceiveInfo object provides parameters to launch a pre-encrypted
 * sev guest or receive the guest during migration. In this mode the images
 * received from the remote is encrypted using transport key, SEV guest would
 * re-encrypt the data using the owner's key creating using the parameters
 * specified in this object.
 *
 * # $QEMU \
 *         -object sev-receive-info,id=launch0,wrapped-tek=xxxx,ten=xxxx \
 *         -object sev-guest,sev=0,launch=launch0 \
 *         .....
 *
 */

struct QSevReceiveInfo {
    Object parent_obj;
    uint32_t handle;
    bool flags_ks;
    char *policy_id;
    uint8_t nonce[16];
    uint8_t dh_pub_qx[32];
    uint8_t dh_pub_qy[32];
    uint8_t policy_measure[32];
    uint8_t wrapped_tek[24];
    uint8_t wrapped_tik[24];
    uint8_t ten[24];
};

struct QSevReceiveInfoClass {
    ObjectClass parent_class;
};

enum SevLaunchMode {
    SEV_LAUNCH_INVALID = 0,
    SEV_LAUNCH_UNENCRYPTED,
    SEV_LAUNCH_ENCRYPTED
};

enum SevState {
    SEV_STATE_INVALID = 0,
    SEV_STATE_LAUNCHING,
    SEV_STATE_RECEIVING,
    SEV_STATE_SENDING,
    SEV_STATE_RUNNING
};

struct SEVState {
    char *launch_id;
    char *sev_info_id;

    uint8_t mode;
    uint8_t state;
    struct kvm_sev_launch_start *launch_start;
    struct kvm_sev_launch_update *launch_update;
    struct kvm_sev_launch_finish *launch_finish;
    struct kvm_sev_receive_start *recv_start;
    struct kvm_sev_receive_update *recv_update;
    struct kvm_sev_receive_finish *recv_finish;
    struct kvm_sev_send_start *send_start;
    struct kvm_sev_send_update *send_update;
    struct kvm_sev_send_finish *send_finish;
};

typedef struct SEVState SEVState;

bool sev_enabled(void);
bool has_sev_guest_policy(const char *keyid);
void *sev_guest_init(const char *keyid);
int sev_guest_launch_start(void *handle);
int sev_guest_launch_finish(void *handle);
void sev_guest_set_ops(void *handle, MemoryRegion *mr);

#endif

