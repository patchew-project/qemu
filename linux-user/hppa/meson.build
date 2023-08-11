syscall_nr_generators += {
  'hppa': generator(sh,
                    arguments: [ meson.current_source_dir() / 'syscallhdr.sh', '@INPUT@', '@OUTPUT@', '@EXTRA_ARGS@' ],
                    output: '@BASENAME@_nr.h')
}

gen = [
  gen_vdso.process('vdso.so', extra_args: ['-r', '__kernel_rt_sigreturn'])
]

linux_user_ss.add(when: 'TARGET_HPPA', if_true: gen)
