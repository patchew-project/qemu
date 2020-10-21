/*
 * QEMU float support
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

static PARTS_TYPE
FUNC(pick_nan)(PARTS_TYPE a, PARTS_TYPE b, float_status *status)
{
    bool a_larger_sig;

    if (is_snan(a.cls) || is_snan(b.cls)) {
        float_raise(float_flag_invalid, status);
    }

    if (status->default_nan_mode) {
        return FUNC(parts_default_nan)(status);
    }

    if (EQ(a.frac, b.frac)) {
        a_larger_sig = a.sign < b.sign;
    } else {
        a_larger_sig = GEU(a.frac, b.frac);
    }

    if (pickNaN(a.cls, b.cls, a_larger_sig, status)) {
        a = b;
    }
    if (is_snan(a.cls)) {
        return FUNC(parts_silence_nan)(a, status);
    }
    return a;
}

/* Canonicalize EXP and FRAC, setting CLS.  */
static PARTS_TYPE
FUNC(sf_canonicalize)(PARTS_TYPE p, const FloatFmt *parm, float_status *status)
{
    if (p.exp == 0) {
        if (likely(EQ0(p.frac))) {
            p.cls = float_class_zero;
        } else if (status->flush_inputs_to_zero) {
            float_raise(float_flag_input_denormal, status);
            p.cls = float_class_zero;
            p.frac = ZERO;
        } else {
            int shift = CLZ(p.frac) - 1;
            p.cls = float_class_normal;
            p.exp = parm->frac_shift - parm->exp_bias - shift + 1;
            p.frac = SHL(p.frac, shift);
        }
    } else if (likely(p.exp < parm->exp_max) || parm->arm_althp) {
        p.cls = float_class_normal;
        p.exp -= parm->exp_bias;
        /* Set implicit bit. */
        p.frac = OR(p.frac, SHL(ONE, parm->frac_size));
        p.frac = SHL(p.frac, parm->frac_shift);
    } else if (likely(EQ0(p.frac))) {
        p.cls = float_class_inf;
    } else {
        p.frac = SHL(p.frac, parm->frac_shift);
        p.cls = (parts_is_snan_frac(HI(p.frac), status)
                 ? float_class_snan : float_class_qnan);
    }
    return p;
}

/* Round and uncanonicalize a floating-point number by parts. There
 * are FRAC_SHIFT bits that may require rounding at the bottom of the
 * fraction; these bits will be removed. The exponent will be biased
 * by EXP_BIAS and must be bounded by [EXP_MAX-1, 0].
 */

static PARTS_TYPE
FUNC(round_canonical)(PARTS_TYPE p, float_status *s, const FloatFmt *parm)
{
    const int exp_max = parm->exp_max;
    const int frac_shift = parm->frac_shift;
    const uint64_t frac_lsb = 1ull << frac_shift;
    const uint64_t frac_lsbm1 = 1ull << (frac_shift - 1);
    const uint64_t round_mask = frac_lsb - 1;
    const uint64_t roundeven_mask = round_mask | frac_lsb;
    int flags = 0;

    switch (p.cls) {
    case float_class_normal:
        {
            bool overflow_norm;
            uint64_t inc, frac_lo;
            int exp;

            frac_lo = LO(p.frac);
            switch (s->float_rounding_mode) {
            case float_round_nearest_even:
                overflow_norm = false;
                inc = ((frac_lo & roundeven_mask) != frac_lsbm1
                       ? frac_lsbm1 : 0);
                break;
            case float_round_ties_away:
                overflow_norm = false;
                inc = frac_lsbm1;
                break;
            case float_round_to_zero:
                overflow_norm = true;
                inc = 0;
                break;
            case float_round_up:
                inc = p.sign ? 0 : round_mask;
                overflow_norm = p.sign;
                break;
            case float_round_down:
                inc = p.sign ? round_mask : 0;
                overflow_norm = !p.sign;
                break;
            case float_round_to_odd:
                overflow_norm = true;
                inc = frac_lo & frac_lsb ? 0 : round_mask;
                break;
            default:
                g_assert_not_reached();
            }

            exp = p.exp + parm->exp_bias;
            if (likely(exp > 0)) {
                if (frac_lo & round_mask) {
                    flags |= float_flag_inexact;
                    p.frac = ADDI(p.frac, inc);
                    if (HI(p.frac) & DECOMPOSED_OVERFLOW_BIT) {
                        p.frac = SHR(p.frac, 1);
                        exp++;
                    }
                }
                p.frac = SHR(p.frac, frac_shift);

                if (parm->arm_althp) {
                    /* ARM Alt HP eschews Inf and NaN for a wider exponent.  */
                    if (unlikely(exp > exp_max)) {
                        /* Overflow.  Return the maximum normal.  */
                        flags = float_flag_invalid;
                        exp = exp_max;
                        p.frac = MONE;
                    }
                } else if (unlikely(exp >= exp_max)) {
                    flags |= float_flag_overflow | float_flag_inexact;
                    if (overflow_norm) {
                        exp = exp_max - 1;
                        p.frac = MONE;
                    } else {
                        p.cls = float_class_inf;
                        goto do_inf;
                    }
                }
            } else if (s->flush_to_zero) {
                flags |= float_flag_output_denormal;
                p.cls = float_class_zero;
                goto do_zero;
            } else {
                bool is_tiny = s->tininess_before_rounding || exp < 0;
                if (!is_tiny) {
                    FRAC_TYPE frac_inc = ADDI(p.frac, inc);
                    if (HI(frac_inc) & DECOMPOSED_OVERFLOW_BIT) {
                        is_tiny = true;
                    }
                }

                p.frac = SHR_JAM(p.frac, 1 - exp);
                frac_lo = LO(p.frac);

                if (frac_lo & round_mask) {
                    /* Need to recompute round-to-even / round-to-odd.  */
                    switch (s->float_rounding_mode) {
                    case float_round_nearest_even:
                        inc = ((frac_lo & roundeven_mask) != frac_lsbm1
                               ? frac_lsbm1 : 0);
                        break;
                    case float_round_to_odd:
                        inc = frac_lo & frac_lsb ? 0 : round_mask;
                        break;
                    default:
                        break;
                    }
                    flags |= float_flag_inexact;
                    p.frac = ADDI(p.frac, inc);
                }

                exp = (HI(p.frac) & DECOMPOSED_IMPLICIT_BIT ? 1 : 0);
                p.frac = SHR(p.frac, frac_shift);

                if (is_tiny && (flags & float_flag_inexact)) {
                    flags |= float_flag_underflow;
                }
                if (exp == 0 && EQ0(p.frac)) {
                    p.cls = float_class_zero;
                }
            }
            p.exp = exp;
        }
        break;

    case float_class_zero:
    do_zero:
        p.exp = 0;
        p.frac = ZERO;
        break;

    case float_class_inf:
    do_inf:
        g_assert(!parm->arm_althp);
        p.exp = exp_max;
        p.frac = ZERO;
        break;

    case float_class_qnan:
    case float_class_snan:
        g_assert(!parm->arm_althp);
        p.exp = exp_max;
        p.frac = SHR(p.frac, parm->frac_shift);
        break;

    default:
        g_assert_not_reached();
    }

    float_raise(flags, s);
    return p;
}

/*
 * Returns the result of adding or subtracting the values of the
 * floating-point values `a' and `b'. The operation is performed
 * according to the IEC/IEEE Standard for Binary Floating-Point
 * Arithmetic.
 */

static PARTS_TYPE
FUNC(addsub_floats)(PARTS_TYPE a, PARTS_TYPE b,
                    bool subtract, float_status *s)
{
    bool a_sign = a.sign;
    bool b_sign = b.sign ^ subtract;

    if (a_sign != b_sign) {
        /* Subtraction */

        if (a.cls == float_class_normal && b.cls == float_class_normal) {
            if (a.exp > b.exp || (a.exp == b.exp && GEU(a.frac, b.frac))) {
                b.frac = SHR_JAM(b.frac, a.exp - b.exp);
                a.frac = SUB(a.frac, b.frac);
            } else {
                a.frac = SHR_JAM(a.frac, b.exp - a.exp);
                a.frac = SUB(b.frac, a.frac);
                a.exp = b.exp;
                a_sign ^= 1;
            }

            if (EQ0(a.frac)) {
                a.cls = float_class_zero;
                a.sign = s->float_rounding_mode == float_round_down;
            } else {
                int shift = CLZ(a.frac) - 1;
                a.frac = SHL(a.frac, shift);
                a.exp = a.exp - shift;
                a.sign = a_sign;
            }
            return a;
        }
        if (is_nan(a.cls) || is_nan(b.cls)) {
            return FUNC(pick_nan)(a, b, s);
        }
        if (a.cls == float_class_inf) {
            if (b.cls == float_class_inf) {
                float_raise(float_flag_invalid, s);
                return FUNC(parts_default_nan)(s);
            }
            return a;
        }
        if (a.cls == float_class_zero && b.cls == float_class_zero) {
            a.sign = s->float_rounding_mode == float_round_down;
            return a;
        }
        if (a.cls == float_class_zero || b.cls == float_class_inf) {
            b.sign = a_sign ^ 1;
            return b;
        }
        if (b.cls == float_class_zero) {
            return a;
        }
    } else {
        /* Addition */
        if (a.cls == float_class_normal && b.cls == float_class_normal) {
            if (a.exp > b.exp) {
                b.frac = SHR_JAM(b.frac, a.exp - b.exp);
            } else if (a.exp < b.exp) {
                a.frac = SHR_JAM(a.frac, b.exp - a.exp);
                a.exp = b.exp;
            }
            a.frac = ADD(a.frac, b.frac);
            if (HI(a.frac) & DECOMPOSED_OVERFLOW_BIT) {
                a.frac = SHR_JAM(a.frac, 1);
                a.exp += 1;
            }
            return a;
        }
        if (is_nan(a.cls) || is_nan(b.cls)) {
            return FUNC(pick_nan)(a, b, s);
        }
        if (a.cls == float_class_inf || b.cls == float_class_zero) {
            return a;
        }
        if (b.cls == float_class_inf || a.cls == float_class_zero) {
            b.sign = b_sign;
            return b;
        }
    }
    g_assert_not_reached();
}
