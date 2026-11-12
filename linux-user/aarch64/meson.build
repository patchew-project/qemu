vdso_be_inc = gen_vdso.process('vdso-be.so',
                               extra_args: ['-r', '__kernel_rt_sigreturn',
                                            '-p', 'vdso_be'])

vdso_le_inc = gen_vdso.process('vdso-le.so',
                               extra_args: ['-r', '__kernel_rt_sigreturn',
                                            '-p', 'vdso_le'])

linux_user_ss.add(when: 'TARGET_AARCH64', if_true: [vdso_be_inc, vdso_le_inc])

linux_user_ss.add(when: 'TARGET_AARCH64', if_true: [files('mte_user_helper.c')])

syscall_nr_generators += {
  'aarch64': generator(sh,
                      arguments: [ meson.current_source_dir() / 'syscallhdr.sh', '@INPUT@', '@OUTPUT@', '@EXTRA_ARGS@' ],
                      output: '@BASENAME@_nr.h')
}
