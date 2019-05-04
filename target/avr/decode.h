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

#ifndef AVR_DECODER_H
#define AVR_DECODER_H

/* Pointer to functions used to do final decoding step from opcode to TCG. */
typedef struct DisasContext DisasContext;
typedef int (*TranslateFn)(DisasContext *ctx, uint32_t opcode);

/*
 * Human readable instruction descriptions used to generate decoder.
 * Doing this at runtime avoids a complicated new build step.
 */
typedef struct {
    /* Instruction mnemonic for debugging */
    const char *name;
    /*
     * Bit pattern describing the instruction's opcode.
     * Each character represents a bit:
     * - '1' means bit must be set
     * - '0' means bit must be cleared
     * - '*' means don't care
     * - '_' is ignored (i.e. whitespace), please use to aid readability
     */
    const char *pattern;
    /* Function used to translate this instruction to TCG */
    TranslateFn decoder;
} Instruction;

/*
 * Converts a list of instruction descriptions to a decoding tree
 * and caches it.
 * Must only be called once.
 * `size` is the number of instructions in the given list.
 */
void avr_decoder_init(const Instruction instructions[], const size_t size);

/*
 * Returns the translation function and length of an instruction, given
 * the opcode.
 * avr_decoder_init() must be called first to build the decoding tree.
 */
TranslateFn avr_decode(const uint32_t opcode, uint32_t *const length_out);

#endif /* AVR_DECODER_H */
