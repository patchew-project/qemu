/*
 * FreeBSD cryptodev definitions for ioctl(2) emulation
 *
 * Copyright (c) 2014 Stacey D. Son
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef BSD_USER_FREEBSD_OS_IOCTL_CRYPTODEV_H
#define BSD_USER_FREEBSD_OS_IOCTL_CRYPTODEV_H

/* see opencrypto/cryptodev.h */

struct target_session_op {
        u_int32_t       cipher;
        u_int32_t       mac;

        u_int32_t       keylen;
        abi_ulong       key;
        int32_t         mackeylen;
        abi_ulong       mackey;

        u_int32_t       ses;
};


struct target_session2_op {
        u_int32_t       cipher;
        u_int32_t       mac;

        u_int32_t       keylen;
        abi_ulong       key;
        int32_t         mackeylen;
        abi_ulong       mackey;

        u_int32_t       ses;
        int32_t         crid;
        int             pad[4];
};

struct target_crypt_find_op {
        int             crid;
        char            name[32];
};

struct target_crparam {
        abi_ulong       crp_p;
        u_int           crp_nbits;
};

#define TARGET_CRK_MAXPARAM     8

struct target_crypt_kop {
        u_int           crk_op;
        u_int           crk_status;
        u_short         crk_iparams;
        u_short         crk_oparams;
        u_int           crk_crid;
        struct target_crparam   crk_param[TARGET_CRK_MAXPARAM];
};

#define TARGET_CRIOGET          TARGET_IOWR('c', 100, u_int32_t)
#define TARGET_CRIOASYMFEAT     TARGET_CIOCASYMFEAT
#define TARGET_CRIOFINDDEV      TARGET_CIOCFINDDEV

#define TARGET_CIOCGSESSION     TARGET_IOWR('c', 101, struct target_session_op)
#define TARGET_CIOCFSESSION     TARGET_IOW('c', 102, u_int32_t)
#define TARGET_CIOCCRYPT        TARGET_IOWR('c', 103, struct target_crypt_op)
#define TARGET_CIOCKEY          TARGET_IOWR('c', 104, struct target_crypt_kop)
#define TARGET_CIOCASYMFEAT     TARGET_IOR('c', 105, u_int32_t)
#define TARGET_CIOCGSESSION2    TARGET_IOWR('c', 106, struct target_session2_op)
#define TARGET_CIOCKEY2         TARGET_IOWR('c', 107, struct target_crypt_kop)
#define TARGET_CIOCFINDDEV TARGET_IOWR('c', 108, struct target_crypt_find_op)

#endif /* BSD_USER_FREEBSD_OS_IOCTL_CRYPTODEV_H */
