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

/* Format-specific handling of exp == exp_max */
typedef enum __attribute__((__packed__)) {
    /* exp==max, frac==0 ? infinity : nan; this is ieee standard. */
    float_expmax_ieee,
    /* exp==max is a normal number; no infinity or nan representation. */
    float_expmax_normal,
    /* exp==max, frac==max ? nan : normal; no infinity representation. */
    float_expmax_e4m3,
} FloatFmtExpMaxKind;

/*
 * Structure holding all of the relevant parameters for a format.
 *   exp_size: the size of the exponent field
 *   exp_bias: the offset applied to the exponent field
 *   exp_max: the maximum normalised exponent
 *   frac_size: the size of the fraction field
 *   frac_shift: shift to normalise the fraction with DECOMPOSED_BINARY_POINT
 * The following are computed based the size of fraction
 *   round_mask: bits below lsb which must be rounded
 * The following optional modifiers are available:
 *   exp_max_kind: affects how exp == exp_max is interpreted
 *   has_explicit_bit: has an explicit integer bit; this affects whether
 *       the float_status floatx80_behaviour handling applies
 *   overflow_raises_invalid: for float_expmax_normal, raise invalid
 *       instead of overflow.
 */
typedef struct {
    int exp_size;
    int exp_bias;
    int exp_re_bias;
    int exp_max;
    int frac_size;
    int frac_shift;
    FloatFmtExpMaxKind exp_max_kind;
    bool has_explicit_bit;
    bool overflow_raises_invalid;
    uint64_t round_mask;
} FloatFmt;

extern const FloatFmt float4_e2m1_params;
extern const FloatFmt float8_e4m3_params;
extern const FloatFmt float8_e5m2_params;
extern const FloatFmt float16_params;
extern const FloatFmt bfloat16_params;
extern const FloatFmt float32_params;
extern const FloatFmt float64_params;
extern const FloatFmt float128_params;

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
