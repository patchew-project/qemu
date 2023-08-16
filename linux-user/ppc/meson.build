syscall_nr_generators += {
  'ppc': generator(sh,
                   arguments: [ meson.current_source_dir() / 'syscallhdr.sh', '@INPUT@', '@OUTPUT@', '@EXTRA_ARGS@' ],
                   output: '@BASENAME@_nr.h')
}

gen32 = [
  gen_vdso.process('vdso-32.so', extra_args: ['-s', '__kernel_sigtramp32',
                                              '-r', '__kernel_sigtramp_rt32']),
]
linux_user_ss.add(when: 'TARGET_PPC', if_true: gen32)

gen64 = [
  gen_vdso.process('vdso-64.so', extra_args: ['-r', '__kernel_sigtramp_rt64']),
  gen_vdso.process('vdso-64le.so', extra_args: ['-r', '__kernel_sigtramp_rt64'])
]
linux_user_ss.add(when: 'TARGET_PPC64', if_true: gen64)
