subdir('nwfpe')

syscall_nr_generators += {
  'arm': generator(sh,
                   arguments: [ meson.current_source_dir() / 'syscallhdr.sh', '@INPUT@', '@OUTPUT@', '@EXTRA_ARGS@' ],
                   output: '@BASENAME@_nr.h')
}

# ??? There does not seem to be a way to do
#   when: ['TARGET_ARM', !'TARGET_WORDS_BIGENDIAN']
# so we'd need to add TARGET_WORDS_LITTLEENDIAN.
# In the meantime, build both files for arm and armeb.

gen = [
  gen_vdso.process('vdso-arm-be.so',
                   extra_args: ['-s', 'sigreturn_codes', '-p', 'vdso_arm']),
  gen_vdso.process('vdso-arm-le.so',
                   extra_args: ['-s', 'sigreturn_codes', '-p', 'vdso_arm']),
  gen_vdso.process('vdso-thm-be.so',
                   extra_args: ['-s', 'sigreturn_codes', '-p', 'vdso_thm']),
  gen_vdso.process('vdso-thm-le.so',
                   extra_args: ['-s', 'sigreturn_codes', '-p', 'vdso_thm']),
]

linux_user_ss.add(when: 'TARGET_ARM', if_true: gen)
