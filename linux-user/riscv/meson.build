gen32 = [
  gen_vdso.process('vdso-32.so', extra_args: ['-r', '__vdso_rt_sigreturn']),
]
gen64 = [
  gen_vdso.process('vdso-64.so', extra_args: ['-r', '__vdso_rt_sigreturn'])
]

linux_user_ss.add(when: 'TARGET_RISCV32', if_true: gen32)
linux_user_ss.add(when: 'TARGET_RISCV64', if_true: gen64)
