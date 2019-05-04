/*
 * AVR instruction decoder.
 *
 * Copyright (c) 2019 University of Kent
 * Author: Sarah Harris
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

/*
 * # Why is this here?
 * This decoder takes a list of human readable descriptions of instructions
 * and uses it to build a binary decision tree used to choose translation
 * functions for opcodes.
 * It's built like this because figuring out the structure of AVR instructions
 * was too hard and writing a Big Nested Switch by hand seemed too painful.
 * This seems to be the simplest answer that doesn't use loads (>0.5MB) of RAM.
 *
 * # How does it work?
 * This is based J. R. Quinlan's ID3 algorithm, tweaked to add weights to each
 * instruction.
 * Having a binary tree branch on opcode bits seems obvious, but the awkward
 * part is deciding which order to test the bits.
 * Getting the order right means that redundant bits can be ignored and fewer
 * branches are needed; i.e. less memory and faster lookups.
 * Here, the tests are ordered by an estimate of information gain based on
 * Shannon Entropy.
 * In short, we guess how much each bit tells us and pick the one that gives
 * us most progress toward knowing which instruction we're seeing.
 * The weights are currently only used to prioritise legal opcodes over
 * illegal opcodes, which significantly reduces the tree size.
 *
 * # Why are you doing this at run time?
 * It was easier than building and running a special purpose tool during
 * QEMU's build process.
 * The tree is only built once, during startup, and hopefully doesn't take long
 * enough to be noticeable.
 */

#include <math.h>
#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/error-report.h"
#include "decode.h"

/* #define DEBUG_DECODER */

/* Wide enough for the largest AVR instruction. */
#define OPCODE_T uint16_t
#define OPCODE_SIZE 16

/*
 * Probability estimate for each instruction.
 * Larger values mean higher priority.
 */
#define WEIGHT_T uint64_t
#define WEIGHT_LEGAL (1 << 16)
#define WEIGHT_ILLEGAL 1

typedef union Tree Tree;

typedef struct {
    bool is_leaf;
    /* Bit to test */
    uint bit;
    const Tree *zero;
    const Tree *one;
} Branch;

typedef struct {
    bool is_leaf;
    TranslateFn decoder;
    /* Instruction length in bits */
    uint32_t length;
    const char *name;
} Leaf;

union Tree {
    bool is_leaf;
    Branch branch;
    Leaf leaf;
};

/* Additional (generated) instruction data */
typedef struct {
    const Instruction *instruction;
    /* Instruction length in bits */
    uint32_t length;
    WEIGHT_T weight;
    /*
     * Bit pattern matched in opcodes.
     * For each 1 in mask, the same bit in the opcode must match that from bits.
     */
    OPCODE_T bits;
    OPCODE_T mask;
} Pattern;

/* Cached decoding tree */
Tree *cache;

/* Return calculated bit pattern and length for an instruction */
static Pattern get_info(const Instruction *instruction)
{
    OPCODE_T bit = 1 << (OPCODE_SIZE - 1);
    OPCODE_T bits = 0;
    OPCODE_T mask = 0;
    uint32_t length = 0;
    const char *c = instruction->pattern;
    while (*c != '\0') {
        switch (*c) {
        case '0':
            mask |= bit;
            bit >>= 1;
            length++;
            break;
        case '1':
            bits |= bit;
            mask |= bit;
            bit >>= 1;
            length++;
            break;
        case '*':
            bit >>= 1;
            length++;
            break;
        case '_':
            /* NOP */
            break;
        default:
            assert(0);
        }
        c++;
    }
    const Pattern pattern = {
        .instruction = instruction,
        .length = length,
        .weight = WEIGHT_LEGAL,
        .bits = bits,
        .mask = mask
    };
    return pattern;
}

/* Return true if an instruction matches a pattern of known/unknown bits */
static bool matches(const Pattern *const pattern, const OPCODE_T bits,
    const OPCODE_T mask)
{
    OPCODE_T overlap = pattern->mask & mask;
    return (pattern->bits & overlap) == (bits & overlap);
}

/* Return number of instructions that match a pattern of known/unknown bits */
static size_t count_legal(const Pattern *patterns, const size_t size,
    const OPCODE_T bits, const OPCODE_T mask)
{
    size_t sum = 0;
    size_t i;
    for (i = 0; i < size; i++) {
        if (matches(&patterns[i], bits, mask)) {
            sum++;
        }
    }
    return sum;
}

/* Return the number of opcodes that could match a bit pattern */
static uint64_t count_opcodes(const OPCODE_T _bits, OPCODE_T mask)
{
    uint64_t matches = 1;
    int i;
    assert(sizeof(matches) * 8 > OPCODE_SIZE);
    for (i = 0; i < OPCODE_SIZE; i++) {
        if (!(mask & 1)) {
            matches <<= 1;
        }
        mask >>= 1;
    }
    return matches;
}

/*
 * Return a known/unknown bit pattern that only matches opcodes matched by both
 * of the given patterns.
 */
static void intersection(const OPCODE_T a_bits, const OPCODE_T a_mask,
    const OPCODE_T b_bits, const OPCODE_T b_mask,
    OPCODE_T *const out_bits, OPCODE_T *const out_mask)
{
    const OPCODE_T overlap = a_mask & b_mask;
    /* The two patterns mustn't have conflicting requirements */
    assert((a_bits & overlap) == (b_bits & overlap));
    *out_bits = (a_bits & a_mask) | (b_bits & b_mask);
    *out_mask = a_mask | b_mask;
}

/*
 * Return one if any opcode allowed by a pattern of known/unknown bits is
 * illegal.
 */
static size_t count_illegal(const Pattern *patterns, const size_t size,
    const OPCODE_T bits, const OPCODE_T mask)
{
    size_t i;
    const uint64_t no_opcodes = count_opcodes(bits, mask);
    uint64_t no_legal = 0;

    /* Count opcodes that match instructions */
    for (i = 0; i < size; i++) {
        const Pattern *pattern = &patterns[i];
        if (matches(pattern, bits, mask)) {
            OPCODE_T both_bits, both_mask;
            intersection(
                bits, mask,
                pattern->bits, pattern->mask,
                &both_bits, &both_mask
            );
            no_legal += count_opcodes(both_bits, both_mask);
        }
    }

    assert(no_legal <= no_opcodes);
    if (no_legal == no_opcodes) {
        return 0;
    } else {
        return 1;
    }
}

/* Return the first matching instruction for a pattern of known/unknown bits */
static const Pattern *find_match(const Pattern *patterns, const size_t size,
    const OPCODE_T bits, const OPCODE_T mask)
{
    size_t i;
    for (i = 0; i < size; i++) {
        if (matches(&patterns[i], bits, mask)) {
            return &patterns[i];
        }
    }
    return NULL;
}

/* Return sum of weights of instructions that match a bit pattern */
static WEIGHT_T weigh_matches(const Pattern *patterns, const size_t size,
    const OPCODE_T bits, const OPCODE_T mask)
{
    size_t i;
    WEIGHT_T illegal = (WEIGHT_T)count_illegal(patterns, size, bits, mask)
        * WEIGHT_ILLEGAL;
    WEIGHT_T legal = 0;
    for (i = 0; i < size; i++) {
        if (matches(&patterns[i], bits, mask)) {
            legal += patterns[i].weight;
        }
    }
    return legal + illegal;
}

/*
 * Return "effort" (estimated information needed) to decide tree outcome.
 * bits and mask give the opcode bits already decided by the parent tree.
 * parent_weight gives the sum of the weights of instructions that the
 * parent tree matches.
 */
static float subtree_effort(const Pattern *patterns, const size_t size,
    const OPCODE_T bits, const OPCODE_T mask, const WEIGHT_T parent_weight)
{
    const WEIGHT_T weight = weigh_matches(patterns, size, bits, mask);
    float entropy_legal, entropy_illegal, probability;
    size_t i;

    /* Sum information needed to decide legal instructions */
    entropy_legal = 0.0;
    for (i = 0; i < size; i++) {
        const Pattern *const pattern = &patterns[i];
        if (matches(pattern, bits, mask)) {
            probability = (float)pattern->weight / (float)weight;
            entropy_legal += -probability * log2(probability);
        }
    }

    /* Sum information needed to decide illegal instructions */
    probability = (float)WEIGHT_ILLEGAL / (float)weight;
    entropy_illegal = -probability * log2(probability) *
        (float)count_illegal(patterns, size, bits, mask);

    return ((float)weight / (float)parent_weight) *
        (entropy_legal + entropy_illegal);
}

/* Return recursively built binary tree for decoding an opcode to instruction */
static Tree *build_tree(const Pattern *patterns, const size_t size,
    OPCODE_T bits, OPCODE_T mask)
{
    /* Check if we've reached a leaf */
    size_t matching_illegal = count_illegal(patterns, size, bits, mask);
    size_t matching_legal = count_legal(patterns, size, bits, mask);
    size_t matching = matching_illegal + matching_legal;
    assert(matching > 0); /* At last an illegal instruction should match */
    if (matching_legal == 0) {
        /* Illegal instruction */
        Leaf *leaf = g_new0(Leaf, 1);
        leaf->is_leaf = true;
        leaf->decoder = NULL;
        leaf->length = 16;
        leaf->name = "illegal";
        return (Tree *)leaf;
    }
    if (matching_legal == 1 && matching_illegal == 0) {
        /* Legal instruction */
        const Pattern *pattern = find_match(patterns, size, bits, mask);
        assert(pattern != NULL);
        const Instruction *const instruction = pattern->instruction;
        Leaf *leaf = g_new0(Leaf, 1);
        leaf->is_leaf = true;
        leaf->decoder = instruction->decoder;
        leaf->length = pattern->length;
        leaf->name = instruction->name;
        return (Tree *)leaf;
    }

    /* Work out which bit to branch on */
    const WEIGHT_T tree_weight = weigh_matches(patterns, size, bits, mask);
    float min_effort = 0.0;
    ssize_t min_bit = -1;
    size_t i;
    for (i = 0; i < OPCODE_SIZE; i++) {
        float effort;
        const OPCODE_T bit = 1 << i;
        if (mask & bit) {
            /* This bit already branched on, skip */
            continue;
        }
        effort = subtree_effort(patterns, size, bits, mask | bit, tree_weight) +
            subtree_effort(patterns, size, bits | bit, mask | bit, tree_weight);
        if (min_bit < 0 || effort < min_effort) {
            min_bit = i;
            min_effort = effort;
        }
    }

    /*
     * Setup branch on bit that gives most information gain.
     * (AKA minimum information/effort needed to decide remaining branches.
     */
    assert(min_bit >= 0); /* Probably multiple instructions match one opcode */
    const OPCODE_T bit = 1 << min_bit;
    const Tree *zero = build_tree(patterns, size, bits, mask | bit);
    const Tree *one = build_tree(patterns, size, bits | bit, mask | bit);
    Branch *branch = g_new0(Branch, 1);
    branch->is_leaf = false;
    branch->bit = min_bit;
    branch->zero = zero;
    branch->one = one;
    return (Tree *)branch;
}

#ifdef DEBUG_DECODER
static size_t depth(const Tree *const tree);
static size_t depth(const Tree *const tree)
{
    if (tree->is_leaf) {
        return 1;
    }
    size_t zero = depth(tree->branch.zero);
    size_t one = depth(tree->branch.one);
    if (zero > one) {
        return zero + 1;
    }
    return one + 1;
}
static size_t count(const Tree *const tree);
static size_t count(const Tree *const tree)
{
    if (tree->is_leaf) {
        return 1;
    }
    return 1 + count(tree->branch.zero) + count(tree->branch.one);
}
#endif

void avr_decoder_init(const Instruction instructions[], const size_t size)
{
    assert(cache == NULL); /* Shouldn't be initialised more than once */
    Pattern *const patterns = g_new0(Pattern, size);
    size_t i;
    for (i = 0; i < size; i++) {
        patterns[i] = get_info(&instructions[i]);
    }
    cache = build_tree(patterns, size, 0, 0);
    g_free(patterns);
#ifdef DEBUG_DECODER
    printf("AVR decoder init, depth=%lu, size=%lu\n",
        depth(cache), count(cache));
#endif
}

TranslateFn avr_decode(const uint32_t opcode, uint32_t *const length_out)
{
    assert(cache != NULL); /* Must be initialised */
    const Tree *node = cache;
    while (1) {
        assert(node != NULL);
        if (node->is_leaf) {
            const Leaf *const leaf = &node->leaf;
            if (leaf->decoder == NULL) {
                /* Illegal instruction */
                error_report("Illegal AVR instruction");
                exit(1);
            }
#ifdef DEBUG_DECODER
            printf("AVR decoder: %s\n", leaf->name);
#endif
            *length_out = leaf->length;
            return leaf->decoder;
        } else {
            const Branch *const branch = &node->branch;
            const OPCODE_T mask = 1 << branch->bit;
            if (opcode & mask) {
                node = branch->one;
            } else {
                node = branch->zero;
            }
        }
    }
}
