/*
 * Target-specific specialization for soft-fp.h.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */


/*
 * Select which NaN to propagate for a two-input operation.
 * IEEE754 doesn't specify all the details of this, so the
 * algorithm is target-specific.
 *
 * A_NAN and B_NAN are positive if their respective operands are SNaN,
 * negative if they are QNaN, or 0 if they are not a NaN at all.
 * The return value is 0 to select NaN A, 1 for NaN B, or 2 to build
 * a new default QNaN.
 *
 * Note that signalling NaNs are always squashed to quiet NaNs
 * by the caller before returning them.
 *
 * A_LARGER is only valid if both A and B are NaNs of some kind,
 * and is true if A has the larger significand, or if both A and B
 * have the same significand but A is positive but B is negative.
 * It is only needed for the x87 tie-break rule.
 */
static inline int pick_nan(int a_nan, int b_nan, bool a_larger,
                           float_status *status)
{
    if (status->default_nan_mode) {
        return 2;
    }
#if defined(TARGET_ARM) || defined(TARGET_HPPA)
    /* ARM mandated NaN propagation rules (see FPProcessNaNs()), take
     * the first of:
     *  1. A if it is signaling
     *  2. B if it is signaling
     *  3. A (quiet)
     *  4. B (quiet)
     * A signaling NaN is always quietened before returning it.
     */
    if (a_nan > 0) {
        return 0;
    } else if (b_nan > 0) {
        return 1;
    } else if (a_nan < 0) {
        return 0;
    } else {
        return 1;
    }
#elif defined(TARGET_MIPS)
    /* According to MIPS specifications, if one of the two operands is
     * a sNaN, a new qNaN has to be generated.  For qNaN inputs the
     * specifications says: "When possible, this QNaN result is one of
     * the operand QNaN values." In practice it seems that most
     * implementations choose the first operand if both operands are qNaN.
     * In short this gives the following rules:
     *  1. A if it is signaling
     *  2. B if it is signaling
     *  3. A (quiet)
     *  4. B (quiet)
     */
    if (a_nan > 0 || b_nan > 0) {
        return 2;
    } else if (a_nan < 0) {
        return 0;
    } else {
        return 1;
    }
#elif defined(TARGET_PPC) || defined(TARGET_XTENSA) || defined(TARGET_M68K)
    /* PowerPC propagation rules:
     *  1. A if it sNaN or qNaN
     *  2. B if it sNaN or qNaN
     * A signaling NaN is always silenced before returning it.
     */
    /* M68000 FAMILY PROGRAMMER'S REFERENCE MANUAL
     * 3.4 FLOATING-POINT INSTRUCTION DETAILS
     * If either operand, but not both operands, of an operation is a
     * nonsignaling NaN, then that NaN is returned as the result. If both
     * operands are nonsignaling NaNs, then the destination operand
     * nonsignaling NaN is returned as the result.
     * If either operand to an operation is a signaling NaN (SNaN), then the
     * SNaN bit is set in the FPSR EXC byte. If the SNaN exception enable bit
     * is set in the FPCR ENABLE byte, then the exception is taken and the
     * destination is not modified. If the SNaN exception enable bit is not
     * set, setting the SNaN bit in the operand to a one converts the SNaN to
     * a nonsignaling NaN. The operation then continues as described in the
     * preceding paragraph for nonsignaling NaNs.
     */
    if (a_nan) {
        return 0;
    } else {
        return 1;
    }
#else
    /* This implements x87 NaN propagation rules:
     * SNaN + QNaN => return the QNaN
     * two SNaNs => return the one with the larger significand, silenced
     * two QNaNs => return the one with the larger significand
     * SNaN and a non-NaN => return the SNaN, silenced
     * QNaN and a non-NaN => return the QNaN
     *
     * If we get down to comparing significands and they are the same,
     * return the NaN with the positive sign bit (if any).
     */
    /* ??? This is x87 specific and should not be the
       default implementation.  */
    if (a_nan > 0) {
        if (b_nan <= 0) {
            return b_nan < 0;
        }
    } else if (a_nan < 0) {
        if (b_nan >= 0) {
            return 0;
        }
    } else {
        return 1;
    }
    return a_larger ^ 1;
#endif
}


/*
 * Select which NaN to propagate for a three-input FMA operation.
 *
 * A_SNAN etc are set iff the operand is an SNaN; QNaN can be
 * determined from (A_CLS == FP_CLS_NAN && !A_SNAN).
 *
 * The return value is 0 to select NaN A, 1 for NaN B, 2 for NaN C,
 * or 3 to build a new default QNaN.
 *
 * Note that signalling NaNs are always squashed to quiet NaNs
 * by the caller before returning them.
 */
static inline int pick_nan_muladd(int a_cls, bool a_snan,
                                  int b_cls, bool b_snan,
                                  int c_cls, bool c_snan,
                                  float_status *status)
{
    /* True if the inner product would itself generate a default NaN.  */
    bool infzero = (a_cls == FP_CLS_INF && b_cls == FP_CLS_ZERO)
                || (b_cls == FP_CLS_INF && a_cls == FP_CLS_ZERO);

#if defined(TARGET_ARM)
    /* For ARM, the (inf,zero,qnan) case sets InvalidOp
     * and returns the default NaN.
     */
    if (infzero && c_cls == FP_CLS_NAN && !c_snan) {
        float_raise(float_flag_invalid, status);
        return 3;
    }

    /* This looks different from the ARM ARM pseudocode, because the ARM ARM
     * puts the operands to a fused mac operation (a*b)+c in the order c,a,b.
     */
    if (c_snan) {
        return 2;
    } else if (a_snan) {
        return 0;
    } else if (b_snan) {
        return 1;
    } else if (c_cls == FP_CLS_NAN) {
        return 2;
    } else if (a_cls == FP_CLS_NAN) {
        return 0;
    } else {
        return 1;
    }
#elif defined(TARGET_MIPS)
    /* For MIPS, the (inf,zero,*) case sets InvalidOp
     * and returns the default NaN.
     */
    if (infzero) {
        float_raise(float_flag_invalid, status);
        return 3;
    }
    if (status->snan_bit_is_one) {
        /* Prefer sNaN over qNaN, in the a, b, c order. */
        if (a_snan) {
            return 0;
        } else if (b_snan) {
            return 1;
        } else if (c_snan) {
            return 2;
        } else if (a_cls == FP_CLS_NAN) {
            return 0;
        } else if (b_cls == FP_CLS_NAN) {
            return 1;
        } else {
            return 2;
        }
    } else {
        /* Prefer sNaN over qNaN, in the c, a, b order. */
        if (c_snan) {
            return 2;
        } else if (a_snan) {
            return 0;
        } else if (b_snan) {
            return 1;
        } else if (c_cls == FP_CLS_NAN) {
            return 2;
        } else if (a_cls == FP_CLS_NAN) {
            return 0;
        } else {
            return 1;
        }
    }
#elif defined(TARGET_PPC)
    /* For PPC, the (inf,zero,qnan) case sets InvalidOp, but we prefer
     * to return an input NaN if we have one (ie c) rather than generating
     * a default NaN
     */
    if (infzero) {
        float_raise(float_flag_invalid, status);
        return 2;
    }

    /* If fRA is a NaN return it; otherwise if fRB is a NaN return it;
     * otherwise return fRC. Note that muladd on PPC is (fRA * fRC) + frB
     */
    if (a_cls == FP_CLS_NAN) {
        return 0;
    } else if (c_cls == FP_CLS_NAN) {
        return 2;
    } else {
        return 1;
    }
#else
    /* A default implementation, which is unlikely to match any
     * real implementation.
     */
    if (infzero) {
        float_raise(float_flag_invalid, status);
    }
    if (a_cls == FP_CLS_NAN) {
        return 0;
    } else if (b_cls == FP_CLS_NAN) {
        return 1;
    } else {
        return 2;
    }
#endif
}
