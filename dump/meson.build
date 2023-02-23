softmmu_ss.add(files('dump-hmp-cmds.c'))

softmmu_ss.add(when: 'CONFIG_SOFTMMU', if_true: [files('dump.c'), snappy, lzo])
specific_ss.add(when: ['CONFIG_SOFTMMU', 'TARGET_X86_64'],
                if_true: files('win_dump-x86.c'),
                if_false: files('win_dump-stub.c'))
