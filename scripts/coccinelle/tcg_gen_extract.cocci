// optimize TCG using extract op
//
// Copyright: (C) 2017 Philippe Mathieu-Daudé. GPLv2+.
// Confidence: Low
// Options: --macro-file scripts/cocci-macro-file.h

@match@ // match shri*+andi* pattern, calls script check_len_correct
identifier ret, arg;
constant ofs, len;
identifier tcg_gen_shri =~ "^tcg_gen_shri_";
identifier tcg_gen_andi =~ "^tcg_gen_andi_";
position p;
@@
(
tcg_gen_shri(ret, arg, ofs);
tcg_gen_andi(ret, ret, len);@p
)

@script:python check_len_correct@
ret_s << match.ret;
arg_s << match.arg;
ofs_s << match.ofs;
msk_s << match.len;
shr_s << match.tcg_gen_shri;
and_s << match.tcg_gen_andi;
@@
try:
    # only eval integer, no #define like 'SR_M'
    ofs = long(ofs_s, 0)
    msk = long(msk_s, 0)
    # get op_size: 32/64
    shr_sz = int(shr_s[-2:])
    and_sz = int(and_s[-2:])
    # op_size shr<and allowed
    # check overflow
    if shr_sz > and_sz or (msk << ofs) >> and_sz:
        cocci.include_match(False) # no further process
except ValueError: # op_size: "tl" not handled yet
    cocci.include_match(False) # no further process

@use_extract depends on check_len_correct@ // ofs/len are valid, we can replace
identifier ret, arg;
constant ofs, len;
@@
(
// from Nikunj A Dadhania comment:
// http://lists.nongnu.org/archive/html/qemu-devel/2017-02/msg05211.html
-tcg_gen_shri_tl(ret, arg, ofs);
-tcg_gen_andi_tl(ret, ret, len);
+tcg_gen_extract_tl(ret, arg, ofs, len);
|
// from Aurelien Jarno comment:
// http://lists.nongnu.org/archive/html/qemu-devel/2017-05/msg01466.html
-tcg_gen_shri_i32(ret, arg, ofs);
-tcg_gen_andi_i32(ret, ret, len);
+tcg_gen_extract_i32(ret, arg, ofs, len);
|
-tcg_gen_shri_i64(ret, arg, ofs);
-tcg_gen_andi_i64(ret, ret, len);
+tcg_gen_extract_i64(ret, arg, ofs, len);
)
