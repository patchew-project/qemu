subdir('nwfpe')

syscall_nr_generators += {
  'arm': generator(sh,
                   arguments: [ meson.current_source_dir() / 'syscallhdr.sh', '@INPUT@', '@OUTPUT@', '@EXTRA_ARGS@' ],
                   output: '@BASENAME@_nr.h')
}

vdso_be8_inc = gen_vdso.process('vdso-be8.so',
                               extra_args: ['-s', 'sigreturn_codes',
                                            '-p', 'vdso_be8'])

vdso_be32_inc = gen_vdso.process('vdso-be32.so',
                               extra_args: ['-s', 'sigreturn_codes',
                                            '-p', 'vdso_be32'])

vdso_le_inc = gen_vdso.process('vdso-le.so',
                               extra_args: ['-s', 'sigreturn_codes'])

linux_user_ss.add(when: 'TARGET_ARM', if_true: [
    vdso_be8_inc, vdso_be32_inc, vdso_le_inc
])
