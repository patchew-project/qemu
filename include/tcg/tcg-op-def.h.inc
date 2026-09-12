/* SPDX-License-Identifier: MIT */

#define TEXT    _i32
#define TYPE    TCG_TYPE_I32
#define TCGV    TCGv_i32
#define TINT    int32_t
#define TTMP    tcgv_i32_temp

#include "tcg-op-def2.h.inc"

#undef TEXT
#undef TYPE
#undef TCGV
#undef TINT
#undef TTMP

#define TEXT    _i64
#define TYPE    TCG_TYPE_I64
#define TCGV    TCGv_i64
#define TINT    int64_t
#define TTMP    tcgv_i64_temp

#include "tcg-op-def2.h.inc"

#undef TEXT
#undef TYPE
#undef TCGV
#undef TINT
#undef TTMP
