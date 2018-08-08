/*
 * Helpers for extracting complex instruction fields.
 *
 * These are referenced in the .decode file and emitted by decodetree.py
 */

/* See e.g. ASR (immediate, predicated).
 * Returns -1 for unallocated encoding; diagnose later.
 */
static inline int tszimm_esz(int x)
{
    x >>= 3;  /* discard imm3 */
    return 31 - clz32(x);
}

static inline int tszimm_shr(int x)
{
    return (16 << tszimm_esz(x)) - x;
}

/* See e.g. LSL (immediate, predicated).  */
static inline int tszimm_shl(int x)
{
    return x - (8 << tszimm_esz(x));
}

static inline int plus1(int x)
{
    return x + 1;
}

/* The SH bit is in bit 8.  Extract the low 8 and shift.  */
static inline int expand_imm_sh8s(int x)
{
    return (int8_t)x << (x & 0x100 ? 8 : 0);
}

static inline int expand_imm_sh8u(int x)
{
    return (uint8_t)x << (x & 0x100 ? 8 : 0);
}

/* Convert a 2-bit memory size (msz) to a 4-bit data type (dtype)
 * with unsigned data.  C.f. SVE Memory Contiguous Load Group.
 */
static inline int msz_dtype(int msz)
{
    static const uint8_t dtype[4] = { 0, 5, 10, 15 };
    return dtype[msz];
}
