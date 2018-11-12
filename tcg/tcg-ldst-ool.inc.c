/*
 * TCG Backend Data: load-store optimization only.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

typedef struct TCGLabelQemuLdstOol {
    QSIMPLEQ_ENTRY(TCGLabelQemuLdstOol) next;
    tcg_insn_unit *label;   /* label pointer to be updated */
    int reloc;              /* relocation type from label_ptr */
    intptr_t addend;        /* relocation addend from label_ptr */
    uint32_t key;           /* oi : is_64 : is_ld */
} TCGLabelQemuLdstOol;


/*
 * Generate TB finalization at the end of block
 */

static tcg_insn_unit *tcg_out_qemu_ldst_ool(TCGContext *s, bool is_ld,
                                            bool is64, TCGMemOpIdx oi);

static bool tcg_out_ldst_ool_finalize(TCGContext *s)
{
    TCGLabelQemuLdstOol *lb;

    /* qemu_ld/st slow paths */
    QSIMPLEQ_FOREACH(lb, &s->ldst_ool_labels, next) {
        gpointer dest, key = (gpointer)(uintptr_t)lb->key;
        TCGMemOpIdx oi;
        bool is_ld, is_64, ok;

        /* If we have generated the thunk, and it's still in range, all ok.  */
        dest = g_hash_table_lookup(s->ldst_ool_thunks, key);
        if (dest &&
            patch_reloc(lb->label, lb->reloc, (intptr_t)dest, lb->addend)) {
            continue;
        }

        /* Generate a new thunk.  */
        is_ld = extract32(lb->key, 0, 1);
        is_64 = extract32(lb->key, 1, 1);
        oi = extract32(lb->key, 2, 30);
        dest = tcg_out_qemu_ldst_ool(s, is_ld, is_64, oi);

        /* Test for (pending) buffer overflow.  The assumption is that any
           one thunk beginning below the high water mark cannot overrun
           the buffer completely.  Thus we can test for overflow after
           generating code without having to check during generation.  */
        if (unlikely((void *)s->code_ptr > s->code_gen_highwater)) {
            return false;
        }

        /* Remember the thunk for next time.  */
        g_hash_table_replace(s->ldst_ool_thunks, key, dest);

        /* The new thunk must be in range.  */
        ok = patch_reloc(lb->label, lb->reloc, (intptr_t)dest, lb->addend);
        tcg_debug_assert(ok);
    }
    return true;
}

/*
 * Allocate a new TCGLabelQemuLdstOol entry.
 */

static void add_ldst_ool_label(TCGContext *s, bool is_ld, bool is_64,
                               TCGMemOpIdx oi, int reloc, intptr_t addend)
{
    TCGLabelQemuLdstOol *lb = tcg_malloc(sizeof(*lb));

    QSIMPLEQ_INSERT_TAIL(&s->ldst_ool_labels, lb, next);
    lb->label = s->code_ptr;
    lb->reloc = reloc;
    lb->addend = addend;
    lb->key = is_ld | (is_64 << 1) | (oi << 2);
}
