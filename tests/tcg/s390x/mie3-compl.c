#include <stdint.h>


#define F_EPI "stg %%r0, %[res] " : [res] "+m" (res) : : "r0", "r2", "r3"

#define F_PRO    asm ( \
    "llihf %%r0,801\n" \
    "lg %%r2, %[a]\n"  \
    "lg %%r3, %[b] "   \
    : : [a] "m" (a),   \
        [b] "m" (b)    \
    : "r2", "r3")

#define FbinOp(S, ASM) uint64_t S(uint64_t a, uint64_t b) \
{ uint64_t res = 0; F_PRO; ASM; return res; }

/* AND WITH COMPLEMENT */
FbinOp(_ncrk,  asm("ncrk  %%r0, %%r3, %%r2\n" F_EPI))
FbinOp(_ncgrk, asm("ncgrk %%r0, %%r3, %%r2\n" F_EPI))

/* NAND */
FbinOp(_nnrk,  asm("nnrk  %%r0, %%r3, %%r2\n" F_EPI))
FbinOp(_nngrk, asm("nngrk %%r0, %%r3, %%r2\n" F_EPI))

/* NOT XOR */
FbinOp(_nxrk,  asm("nxrk  %%r0, %%r3, %%r2\n" F_EPI))
FbinOp(_nxgrk, asm("nxgrk %%r0, %%r3, %%r2\n" F_EPI))

/* NOR */
FbinOp(_nork,  asm("nork  %%r0, %%r3, %%r2\n" F_EPI))
FbinOp(_nogrk, asm("nogrk %%r0, %%r3, %%r2\n" F_EPI))

/* OR WITH COMPLEMENT */
FbinOp(_ocrk,  asm("ocrk  %%r0, %%r3, %%r2\n" F_EPI))
FbinOp(_ocgrk, asm("ocgrk %%r0, %%r3, %%r2\n" F_EPI))


int main(int argc, char *argv[])
{
    if (_ncrk(0xFF88, 0xAA11)  != 0x0000032100000011ull ||
        _nnrk(0xFF88, 0xAA11)  != 0x00000321FFFF55FFull ||
        _nork(0xFF88, 0xAA11)  != 0x00000321FFFF0066ull ||
        _nxrk(0xFF88, 0xAA11)  != 0x00000321FFFFAA66ull ||
        _ocrk(0xFF88, 0xAA11)  != 0x00000321FFFFAA77ull ||
        _ncgrk(0xFF88, 0xAA11) != 0x0000000000000011ull ||
        _nngrk(0xFF88, 0xAA11) != 0xFFFFFFFFFFFF55FFull ||
        _nogrk(0xFF88, 0xAA11) != 0xFFFFFFFFFFFF0066ull ||
        _nxgrk(0xFF88, 0xAA11) != 0xFFFFFFFFFFFFAA66ull ||
        _ocgrk(0xFF88, 0xAA11) != 0xFFFFFFFFFFFFAA77ull)
    {
        return 1;
    }

    return 0;
}
