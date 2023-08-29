syscall_nr_generators += {
  's390x': generator(sh,
                     arguments: [ meson.current_source_dir() / 'syscallhdr.sh', '@INPUT@', '@OUTPUT@', '@EXTRA_ARGS@' ],
                     output: '@BASENAME@_nr.h')
}

vdso_so = custom_target(output: 'vdso.so',
                        input: files('vdso.S', 'vdso.ld'),
                        depend_files: files('vdso-asmoffset.h'),
                        command: [
                            build_vdso_cmd,
                            '-B', meson.project_build_root(),
                            '-C', meson.current_source_dir(),
                            '-T', 's390x-linux-user',
                            '-o', '@OUTPUT@',
                            '--', '-nostdlib', '-shared',
                            '-Wl,-h,linux-vdso64.so.1',
                            '-Wl,--build-id=sha1',
                            '-Wl,--hash-style=both',
                            '-Wl,-T,@INPUT1@',
                            '@INPUT0@'
                        ])

vdso_inc = gen_vdso.process('vdso.so', extra_args: [
                                '-s', '__kernel_sigreturn',
                                '-r', '__kernel_rt_sigreturn'
                            ])

linux_user_ss.add(when: 'TARGET_S390X', if_true: vdso_inc)
