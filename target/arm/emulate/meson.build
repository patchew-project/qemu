gen_a64_ldst = decodetree.process('a64-ldst.decode',
    extra_args: ['--static-decode=decode_a64_ldst'])

arm_common_system_ss.add(when: 'TARGET_AARCH64', if_true: [
    gen_a64_ldst, files('arm_emulate.c')
])

# Static library for unit testing (links emulation code + decodetree decoder)
arm_emulate_test_lib = static_library('arm-emulate-test',
    sources: [files('arm_emulate.c'), gen_a64_ldst],
    dependencies: [qemuutil],
    include_directories: include_directories('.'))

arm_emulate_test = declare_dependency(
    link_with: arm_emulate_test_lib,
    include_directories: include_directories('.'))
