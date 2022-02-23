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
FbinOp(_ncrk,  asm(".insn rrf, 0xB9F50000, %%r0, %%r3, %%r2, 0\n" F_EPI))
FbinOp(_ncgrk, asm(".insn rrf, 0xB9E50000, %%r0, %%r3, %%r2, 0\n" F_EPI))

/* NAND */
FbinOp(_nnrk,  asm(".insn rrf, 0xB9740000, %%r0, %%r3, %%r2, 0\n" F_EPI))
FbinOp(_nngrk, asm(".insn rrf, 0xB9640000, %%r0, %%r3, %%r2, 0\n" F_EPI))

/* NOT XOR */
FbinOp(_nxrk,  asm(".insn rrf, 0xB9770000, %%r0, %%r3, %%r2, 0\n" F_EPI))
FbinOp(_nxgrk, asm(".insn rrf, 0xB9670000, %%r0, %%r3, %%r2, 0\n" F_EPI))

/* NOR */
FbinOp(_nork,  asm(".insn rrf, 0xB9760000, %%r0, %%r3, %%r2, 0\n" F_EPI))
FbinOp(_nogrk, asm(".insn rrf, 0xB9660000, %%r0, %%r3, %%r2, 0\n" F_EPI))

/* OR WITH COMPLEMENT */
FbinOp(_ocrk,  asm(".insn rrf, 0xB9750000, %%r0, %%r3, %%r2, 0\n" F_EPI))
FbinOp(_ocgrk, asm(".insn rrf, 0xB9650000, %%r0, %%r3, %%r2, 0\n" F_EPI))


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
