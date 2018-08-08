/*
 * Fallback dissasembly
 */
#include "qemu/osdep.h"
#include "cpu.h"
#include "internals.h"
#include "decoder.h"

#include "disas-sve.inc.c"

size_t do_aarch64_fallback_disassembly(const uint8_t *insnp, char *ptr, size_t n)
{
    uint32_t insn = ldl_p(insnp);

    if (insn == 0x5af0) {
        snprintf(ptr, n, "illegal insn (risu checkpoint?)");
    } else if (!decode(ptr, n, insn)) {
        snprintf(ptr, n, "failed decode");
    }

    return 4;
}
