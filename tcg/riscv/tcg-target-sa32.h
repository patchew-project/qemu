/*
 * Do not set TCG_TARGET_SIGNED_ADDR32 for RV32;
 * TCG expects this to only be set for 64-bit hosts.
 */
#define TCG_TARGET_SIGNED_ADDR32  (__riscv_xlen == 64)
