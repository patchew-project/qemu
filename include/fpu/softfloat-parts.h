/*
 * Floating point intermediate representation
 *
 * The code in this source file is derived from release 2a of the SoftFloat
 * IEC/IEEE Floating-point Arithmetic Package. Those parts of the code (and
 * some later contributions) are provided under that license, as detailed below.
 * It has subsequently been modified by contributors to the QEMU Project,
 * so some portions are provided under:
 *  the SoftFloat-2a license
 *  the BSD license
 *  GPL-v2-or-later
 *
 * Any future contributions to this file after December 1st 2014 will be
 * taken to be licensed under the Softfloat-2a license unless specifically
 * indicated otherwise.
 */

#ifndef SOFTFLOAT_PARTS_H
#define SOFTFLOAT_PARTS_H

/*
 * Classify a floating point number. Everything above float_class_qnan
 * is a NaN so cls >= float_class_qnan is any NaN.
 *
 * Note that we canonicalize denormals, so most code should treat
 * class_normal and class_denormal identically.
 */

typedef enum __attribute__ ((__packed__)) {
    float_class_unclassified,
    float_class_zero,
    float_class_normal,
    float_class_denormal, /* input was a non-squashed denormal */
    float_class_inf,
    float_class_qnan,  /* all NaNs from here */
    float_class_snan,
} FloatClass;

#define float_cmask(bit)  (1u << (bit))

enum {
    float_cmask_zero    = float_cmask(float_class_zero),
    float_cmask_normal  = float_cmask(float_class_normal),
    float_cmask_denormal = float_cmask(float_class_denormal),
    float_cmask_inf     = float_cmask(float_class_inf),
    float_cmask_qnan    = float_cmask(float_class_qnan),
    float_cmask_snan    = float_cmask(float_class_snan),

    float_cmask_infzero = float_cmask_zero | float_cmask_inf,
    float_cmask_anynan  = float_cmask_qnan | float_cmask_snan,
    float_cmask_anynorm = float_cmask_normal | float_cmask_denormal,
};

/*
 * Structure holding all of the decomposed parts of a float.
 * The exponent is unbiased and the fraction is normalized.
 *
 * The fraction words are stored in big-endian word ordering,
 * so that truncation from a larger format to a smaller format
 * can be done simply by ignoring subsequent elements.
 */

typedef struct {
    FloatClass cls;
    bool sign;
    int32_t exp;
    union {
        /* Routines that know the structure may reference the singular name. */
        uint64_t frac;
        /*
         * Routines expanded with multiple structures reference "hi" and "lo"
         * depending on the operation.  In FloatParts64, "hi" and "lo" are
         * both the same word and aliased here.
         */
        uint64_t frac_hi;
        uint64_t frac_lo;
    };
} FloatParts64;

typedef struct {
    FloatClass cls;
    bool sign;
    int32_t exp;
    uint64_t frac_hi;
    uint64_t frac_lo;
} FloatParts128;

#endif
