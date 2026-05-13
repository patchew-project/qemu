/* SPDX-License-Identifier: GPL-2.0-or-later */
ARM_PROP("cpu_implementer", NUM, 0x41),
ARM_PROP("cpu_variant", NUM, 0x0),
ARM_PROP("cpu_architecture", NUM, 0xF),
ARM_PROP("cpu_partnum", NUM, 0xD4F),
ARM_PROP("cpu_revision", NUM, 0x2),

ARM_PROP("hw_prop_BRPS", NUM, 0x5),
ARM_PROP("hw_prop_WRPs", NUM, 0x3),
ARM_PROP("hw_prop_CTX_CMPs", NUM, 0x1),
ARM_PROP("feat_PMU", STR, "v3p5"),

ARM_PROP("feat_TLB", STR, "range"),

ARM_PROP("feat_BF16", STR, "on"),
ARM_PROP("feat_DGH", STR, "on"),
ARM_PROP("feat_I8MM", STR, "on"),

ARM_PROP("feat_FP", STR, "fp16"),
ARM_PROP("feat_AdvSIMD", STR, "fp16"),
ARM_PROP("feat_RAS", STR, "1.1_base"),

/*
 * V2 silicon may report CSV2=2 (FEAT_CSV2_2) per TRM page 392, but
 * KVM clamps the guest-visible limit to 1.
 */
ARM_PROP("feat_CSV2", STR, "1.0"),


ARM_PROP("hw_prop_PARANGE", STR, "48"),
ARM_PROP("hw_prop_ASIDBITS", STR, "16"),
ARM_PROP("feat_BIGEND", STR, "on"),
ARM_PROP("feat_SNSMEM", STR, "on"),
ARM_PROP("hw_prop_TGRAN16", STR, "on"),
ARM_PROP("hw_prop_TGRAN64", STR, "on"),
ARM_PROP("hw_prop_TGRAN4", STR, "on"),
ARM_PROP("hw_prop_TGRAN16_2", STR, "on"),
ARM_PROP("hw_prop_TGRAN64_2", STR, "on"),
ARM_PROP("hw_prop_TGRAN4_2", STR, "on"),

ARM_PROP("feat_HAFDBS", STR, "dbm"),
ARM_PROP("hw_prop_VMIDBITS", STR, "16"),
ARM_PROP("feat_HPDS", STR, "hpds2"),
ARM_PROP("feat_PAN", STR, "pan3"),
ARM_PROP("feat_ECBHB", STR, "off"),
ARM_PROP("feat_SpecSEI", STR, "off"),

ARM_PROP("hw_prop_FWB", STR, "on"),
ARM_PROP("hw_prop_ST", STR, "48_47"),
ARM_PROP("feat_TTL", STR, "on"),
ARM_PROP("feat_BBM", STR, "2"),
ARM_PROP("feat_EVT", STR, "ttlbxs"),
ARM_PROP("feat_E2H0", STR, "on"),


ARM_PROP("hw_prop_IMInline", NUM, 4),
ARM_PROP("hw_prop_L1IP", STR, "pipt"),
ARM_PROP("hw_prop_DMInline", NUM, 4),
ARM_PROP("hw_prop_ERG", NUM, 4),
ARM_PROP("hw_prop_CWG", NUM, 4),

ARM_PROP("hw_prop_BS", NUM, 0x4),

ARM_PROP_END,
