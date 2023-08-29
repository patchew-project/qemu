vdso_so = custom_target(output: 'vdso.so',
                        input: files('vdso.S', 'vdso.ld'),
                        depend_files: files('vdso-asmoffset.h'),
                        command: [
                            build_vdso_cmd,
                            '-B', meson.project_build_root(),
                            '-C', meson.current_source_dir(),
                            '-T', 'loongarch64-linux-user',
                            '-o', '@OUTPUT@',
	                    '--', '-nostdlib', '-shared', '-fpic',
	                    '-Wl,-h,linux-vdso.so.1',
                            '-Wl,--build-id=sha1',
                            '-Wl,--hash-style=both',
                            '-Wl,--no-warn-rwx-segments',
                            '-Wl,-T,@INPUT1@',
                            '@INPUT0@'
                        ])

vdso_inc = gen_vdso.process(vdso_so, extra_args: ['-r', '__vdso_rt_sigreturn'])

linux_user_ss.add(when: 'TARGET_LOONGARCH64', if_true: vdso_inc)
