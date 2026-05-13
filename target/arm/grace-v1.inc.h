
/* SPDX-License-Identifier: GPL-2.0-or-later */
ARM_PROP("cpu_revision", NUM, 0x0),

ARM_PROP("feat_AES", STR, "pmull"),
ARM_PROP("feat_SHA1", STR, "on"),
ARM_PROP("feat_SHA2", STR, "sha512"),
ARM_PROP("feat_SHA3", STR, "on"),
ARM_PROP("feat_SM3", STR, "on"),
ARM_PROP("feat_SM4", STR, "on"),

ARM_PROP("hw_prop_IDC", BOOL, true),
ARM_PROP("hw_prop_DIC", BOOL, true),

ARM_PROP("cpu_revidr", NUM, 1),

ARM_PROP_END,
