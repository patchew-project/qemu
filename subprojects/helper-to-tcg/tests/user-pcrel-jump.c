/*
 * Test translation custom jump instructions
 *
 * (experimental)
 */

#define ANNOTATE(str) __attribute__((annotate("helper-to-tcg")))

ANNOTATE("immediate: 0")
void target_pcrel_jump(int pc);
ANNOTATE("immediate: 0")
void target_pcrel_jump_cond(int pc);
ANNOTATE("immediate: 0")
void target_pcrel_jump_fall(int pc);

ANNOTATE("helper-to-tcg")
void prcel_jump(int off) {
    int ea = 4*off + 3;
    target_pcrel_jump(ea);
}

ANNOTATE("helper-to-tcg")
void prcel_condjump(int cond, int off) {
    if (cond) {
        int ea = 4*off + 3;
        target_pcrel_jump(ea);
    }
}
