# TARGET_BIG_ENDIAN is defined to 'n' for little-endian; which means it
# is always true as far as source_set.apply() is concerned.  Always build
# both header files and include the right one via #if.

vdso_cmd = [
    build_vdso_cmd,
    '-B', meson.project_build_root(),
    '-C', meson.current_source_dir(),
    '-T', 'aarch64-linux-user',
    '-o', '@OUTPUT@',
    '--',
    '-nostdlib', '-shared',
    '-Wl,-h,linux-vdso.so.1',
    '-Wl,--build-id=sha1',
    '-Wl,--hash-style=both',
    '-Wl,-T,@INPUT1@',
    '@INPUT0@'
]

vdso_le_so = custom_target(output: 'vdso-le.so',
                           input: files('vdso.S', 'vdso.ld'),
                           command: vdso_cmd + ['-mlittle-endian'])

vdso_be_so = custom_target(output: 'vdso-be.so',
                           input: files('vdso.S', 'vdso.ld'),
                           command: vdso_cmd + ['-mbig-endian'])

gen = [
  gen_vdso.process(vdso_le_so, extra_args: ['-r', '__kernel_rt_sigreturn']),
  gen_vdso.process(vdso_be_so, extra_args: ['-r', '__kernel_rt_sigreturn']),
]

linux_user_ss.add(when: 'TARGET_AARCH64', if_true: gen)
