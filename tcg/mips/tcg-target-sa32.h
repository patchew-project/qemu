/*
 * Do not set TCG_TARGET_SIGNED_ADDR32 for mips32;
 * TCG expects this to only be set for 64-bit hosts.
 */
#ifdef __mips64
#define TCG_TARGET_SIGNED_ADDR32 1
#else
#define TCG_TARGET_SIGNED_ADDR32 0
#endif
