/*
 * AArch64 has both SXTW and UXTW addressing modes, which means that
 * it is agnostic to how guest addresses should be represented.
 * Because aarch64 is more common than the other hosts that will
 * want to use this feature, enable it for continuous testing.
 */
#define TCG_TARGET_SIGNED_ADDR32 1
