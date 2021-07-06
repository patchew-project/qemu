# ??? There does not seem to be a way to do
#   when: ['TARGET_AARCH64', !'TARGET_WORDS_BIGENDIAN']
# so we'd need to add TARGET_WORDS_LITTLEENDIAN.
# In the meantime, build both files for aarch64 and aarch64_be.

gen = [
  gen_vdso.process('vdso-be.so', extra_args: ['-r', '__kernel_rt_sigreturn']),
  gen_vdso.process('vdso-le.so', extra_args: ['-r', '__kernel_rt_sigreturn'])
]

linux_user_ss.add(when: 'TARGET_AARCH64', if_true: gen)
