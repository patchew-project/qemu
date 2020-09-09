/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * TCG backend data: operand constaints.
 * Copyright (c) 2020 Linaro
 */

/*
 * Define structures for each set of constraints.
 */

#define C_PFX1(P, A)                 P##A
#define C_PFX2(P, A, B)              P##A##_##B
#define C_PFX3(P, A, B, C)           P##A##_##B##_##C
#define C_PFX4(P, A, B, C, D)        P##A##_##B##_##C##_##D
#define C_PFX5(P, A, B, C, D, E)     P##A##_##B##_##C##_##D##_##E
#define C_PFX6(P, A, B, C, D, E, F)  P##A##_##B##_##C##_##D##_##E##_##F

#define C_O0_I1(I1) \
    static const TCGTargetOpDef C_PFX1(c_o0_i1_, I1) \
      = { .args_ct_str = { #I1 } };

#define C_O0_I2(I1, I2) \
    static const TCGTargetOpDef C_PFX2(c_o0_i2_, I1, I2) \
      = { .args_ct_str = { #I1, #I2 } };

#define C_O0_I3(I1, I2, I3) \
    static const TCGTargetOpDef C_PFX3(c_o0_i3_, I1, I2, I3) \
      = { .args_ct_str = { #I1, #I2, #I3 } };

#define C_O0_I4(I1, I2, I3, I4) \
    static const TCGTargetOpDef C_PFX4(c_o0_i4_, I1, I2, I3, I4) \
      = { .args_ct_str = { #I1, #I2, #I3, #I4 } };

#define C_O1_I1(O1, I1) \
    static const TCGTargetOpDef C_PFX2(c_o1_i1_, O1, I1) \
      = { .args_ct_str = { #O1, #I1 } };

#define C_O1_I2(O1, I1, I2) \
    static const TCGTargetOpDef C_PFX3(c_o1_i2_, O1, I1, I2) \
      = { .args_ct_str = { #O1, #I1, #I2 } };

#define C_O1_I3(O1, I1, I2, I3) \
    static const TCGTargetOpDef C_PFX4(c_o1_i3_, O1, I1, I2, I3) \
      = { .args_ct_str = { #O1, #I1, #I2, #I3 } };

#define C_O1_I4(O1, I1, I2, I3, I4) \
    static const TCGTargetOpDef C_PFX5(c_o1_i4_, O1, I1, I2, I3, I4) \
      = { .args_ct_str = { #O1, #I1, #I2, #I3, #I4 } };

#define C_N1_I2(O1, I1, I2) \
    static const TCGTargetOpDef C_PFX3(c_n1_i2_, O1, I1, I2) \
      = { .args_ct_str = { "&" #O1, #I1, #I2 } };

#define C_O2_I1(O1, O2, I1) \
    static const TCGTargetOpDef C_PFX3(c_o2_i1_, O1, O2, I1) \
      = { .args_ct_str = { #O1, #O2, #I1 } };

#define C_O2_I2(O1, O2, I1, I2) \
    static const TCGTargetOpDef C_PFX4(c_o2_i2_, O1, O2, I1, I2) \
      = { .args_ct_str = { #O1, #O2, #I1, #I2 } };

#define C_O2_I3(O1, O2, I1, I2, I3) \
    static const TCGTargetOpDef C_PFX5(c_o2_i3_, O1, O2, I1, I2, I3) \
      = { .args_ct_str = { #O1, #O2, #I1, #I2, #I3 } };

#define C_O2_I4(O1, O2, I1, I2, I3, I4) \
    static const TCGTargetOpDef C_PFX6(c_o2_i4_, O1, O2, I1, I2, I3, I4) \
      = { .args_ct_str = { #O1, #O2, #I1, #I2, #I3, #I4 } };

#include "tcg-target-constr.h"


/*
 * Redefine the macros so that they now reference those structures.
 * These values should be returned from tcg_target_op_def().
 */

#undef C_O0_I1
#undef C_O0_I2
#undef C_O0_I3
#undef C_O0_I4
#undef C_O1_I1
#undef C_O1_I2
#undef C_O1_I3
#undef C_O1_I4
#undef C_N1_I2
#undef C_O2_I1
#undef C_O2_I2
#undef C_O2_I3
#undef C_O2_I4

#define C_O0_I1(I1)                     &C_PFX1(c_o0_i1_, I1)
#define C_O0_I2(I1, I2)                 &C_PFX2(c_o0_i2_, I1, I2)
#define C_O0_I3(I1, I2, I3)             &C_PFX3(c_o0_i3_, I1, I2, I3)
#define C_O0_I4(I1, I2, I3, I4)         &C_PFX4(c_o0_i4_, I1, I2, I3, I4)

#define C_O1_I1(O1, I1)                 &C_PFX2(c_o1_i1_, O1, I1)
#define C_O1_I2(O1, I1, I2)             &C_PFX3(c_o1_i2_, O1, I1, I2)
#define C_O1_I3(O1, I1, I2, I3)         &C_PFX4(c_o1_i3_, O1, I1, I2, I3)
#define C_O1_I4(O1, I1, I2, I3, I4)     &C_PFX5(c_o1_i4_, O1, I1, I2, I3, I4)

#define C_N1_I2(O1, I1, I2)             &C_PFX3(c_n1_i2_, O1, I1, I2)

#define C_O2_I1(O1, O2, I1)             &C_PFX3(c_o2_i1_, O1, O2, I1)
#define C_O2_I2(O1, O2, I1, I2)         &C_PFX4(c_o2_i2_, O1, O2, I1, I2)
#define C_O2_I3(O1, O2, I1, I2, I3)     &C_PFX5(c_o2_i3_, O1, O2, I1, I2, I3)
#define C_O2_I4(O1, O2, I1, I2, I3, I4) \
    &C_PFX6(c_o2_i4_, O1, O2, I1, I2, I3, I4)
