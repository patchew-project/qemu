# SPDX-License-Identifier: GPL-2.0-or-later

gen_a64_ldst = decodetree.process('a64-ldst.decode',
    extra_args: ['--static-decode=decode_a64_ldst'])

arm_common_system_ss.add(when: 'TARGET_AARCH64', if_true: [
    gen_a64_ldst, files('arm_emulate.c')
])
