syscall_nr_generators += {
  'x86_64': generator(sh,
                      arguments: [ meson.current_source_dir() / 'syscallhdr.sh', '@INPUT@', '@OUTPUT@', '@EXTRA_ARGS@' ],
                      output: '@BASENAME@_nr.h')
}

vdso_so = custom_target(output: 'vdso.so',
                        input: files('vdso.S', 'vdso.ld'),
                        command: [
                            build_vdso_cmd,
                            '-B', meson.project_build_root(),
                            '-C', meson.current_source_dir(),
                            '-T', 'x86_64-linux-user',
                            '-o', '@OUTPUT@',
	                    '--', '-nostdlib', '-shared',
	                    '-Wl,-h,linux-vdso.so.1',
                            '-Wl,--build-id=sha1',
                            '-Wl,--hash-style=both',
                            '-Wl,-T,@INPUT1@',
                            '@INPUT0@'
                        ])

vdso_inc = gen_vdso.process(vdso_so)

linux_user_ss.add(when: 'TARGET_X86_64', if_true: vdso_inc)
