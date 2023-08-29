vdso_cmd = [
    build_vdso_cmd,
    '-B', meson.project_build_root(),
    '-C', meson.current_source_dir(),
    '-T', 'riscv64-linux-user',
    '-o', '@OUTPUT@',
    '--',
    '-nostdlib', '-shared', '-fpic',
    '-Wl,-h,linux-vdso.so.1',
    '-Wl,--build-id=sha1',
    '-Wl,--hash-style=both',
    '-Wl,-T,@INPUT1@',
    '@INPUT0@'
]

vdso_32_so = custom_target(output: 'vdso-32.so',
                           input: files('vdso.S', 'vdso.ld'),
                           depend_files: files('vdso-asmoffset.h'),
                           command: vdso_cmd + ['-mabi=ilp32d', '-march=rv32g'])

vdso_64_so = custom_target(output: 'vdso-64.so',
                           input: files('vdso.S', 'vdso.ld'),
                           depend_files: files('vdso-asmoffset.h'),
                           command: vdso_cmd + ['-mabi=lp64d', '-march=rv64g'])

vdso_32_inc = gen_vdso.process(vdso_32_so, extra_args: ['-r', '__vdso_rt_sigreturn'])
vdso_64_inc = gen_vdso.process(vdso_64_so, extra_args: ['-r', '__vdso_rt_sigreturn'])

linux_user_ss.add(when: 'TARGET_RISCV32', if_true: vdso_32_inc)
linux_user_ss.add(when: 'TARGET_RISCV64', if_true: vdso_64_inc)
