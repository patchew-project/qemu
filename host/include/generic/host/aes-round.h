/*
 * No host specific aes acceleration.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HOST_AES_ROUND_H
#define HOST_AES_ROUND_H

#define HAVE_AES_ACCEL  false
#define ATTR_AES_ACCEL

void aesenc_SB_SR_accel(AESState *, const AESState *, bool)
    QEMU_ERROR("unsupported accel");

#endif
