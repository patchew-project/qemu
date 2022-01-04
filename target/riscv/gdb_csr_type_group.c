/* Copyright 2021 Siemens AG */
#include "qemu/osdep.h"
#include "cpu.h"
#include "gdb_csr_type_group.h"

struct riscv_gdb_csr_tg const riscv_gdb_csr_type_group[] = {

#if !defined(CONFIG_USER_ONLY)
#  ifdef TARGET_RISCV64
#    include "csr64-op-gdbserver.h"
#  elif defined TARGET_RISCV64
#    include "csr32-op-gdbserver.h"
#  endif
#endif /* !CONFIG_USER_ONLY */

};
