/*
 * No host specific pack and unpack acceleration.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef GENERIC_HOST_VEC_PACK_H
#define GENERIC_HOST_VEC_PACK_H

#define HAVE_VEC_PACK_ACCEL  false

void vec_pack_sswb_64_accel(void *, const void *, const void *)
    QEMU_ERROR("unsupported accel");
void vec_pack_uswb_64_accel(void *, const void *, const void *)
    QEMU_ERROR("unsupported accel");
void vec_pack_ssdw_64_accel(void *, const void *, const void *)
    QEMU_ERROR("unsupported accel");
void vec_unpck_lbw_64_accel(void *, const void *, const void *)
    QEMU_ERROR("unsupported accel");
void vec_unpck_lwd_64_accel(void *, const void *, const void *)
    QEMU_ERROR("unsupported accel");
void vec_unpck_ldq_64_accel(void *, const void *, const void *)
    QEMU_ERROR("unsupported accel");
void vec_unpck_hbw_64_accel(void *, const void *, const void *)
    QEMU_ERROR("unsupported accel");
void vec_unpck_hwd_64_accel(void *, const void *, const void *)
    QEMU_ERROR("unsupported accel");
void vec_unpck_hdq_64_accel(void *, const void *, const void *)
    QEMU_ERROR("unsupported accel");

void vec_pack_sswb_128_accel(void *, const void *, const void *)
    QEMU_ERROR("unsupported accel");
void vec_pack_uswb_128_accel(void *, const void *, const void *)
    QEMU_ERROR("unsupported accel");
void vec_pack_ssdw_128_accel(void *, const void *, const void *)
    QEMU_ERROR("unsupported accel");
void vec_unpck_lbw_128_accel(void *, const void *, const void *)
    QEMU_ERROR("unsupported accel");
void vec_unpck_lwd_128_accel(void *, const void *, const void *)
    QEMU_ERROR("unsupported accel");
void vec_unpck_ldq_128_accel(void *, const void *, const void *)
    QEMU_ERROR("unsupported accel");
void vec_unpck_hbw_128_accel(void *, const void *, const void *)
    QEMU_ERROR("unsupported accel");
void vec_unpck_hwd_128_accel(void *, const void *, const void *)
    QEMU_ERROR("unsupported accel");
void vec_unpck_hdq_128_accel(void *, const void *, const void *)
    QEMU_ERROR("unsupported accel");

#endif /* GENERIC_HOST_VEC_PACK_H */
