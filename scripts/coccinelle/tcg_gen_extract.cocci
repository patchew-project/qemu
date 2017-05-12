// optimize TCG using extract op
//
// Copyright: (C) 2017 Philippe Mathieu-Daudé. GPLv2+.
// Confidence: High
// Options: --macro-file scripts/cocci-macro-file.h
//
// Nikunj A Dadhania optimization:
// http://lists.nongnu.org/archive/html/qemu-devel/2017-02/msg05211.html
// Aurelien Jarno optimization:
// http://lists.nongnu.org/archive/html/qemu-devel/2017-05/msg01466.html
// Coccinelle helpful issue:
// https://github.com/coccinelle/coccinelle/issues/86

@match@ // match shri*+andi* pattern, calls script verify_len
identifier ret, arg;
constant ofs, len;
identifier shr_fn =~ "^tcg_gen_shri_";
identifier and_fn =~ "^tcg_gen_andi_";
position shr_p;
position and_p;
@@
(
shr_fn@shr_p(ret, arg, ofs);
and_fn@and_p(ret, ret, len);
)

@script:python verify_len@
ret_s << match.ret;
len_s << match.len;
shr_s << match.shr_fn;
and_s << match.and_fn;
shr_p << match.shr_p;
extract_fn;
@@
print "candidate at %s:%s" % (shr_p[0].file, shr_p[0].line)
len_fn=len("tcg_gen_shri_")
shr_sz=shr_s[len_fn:]
and_sz=and_s[len_fn:]
# TODO: op_size shr<and allowed?
is_same_op_size = shr_sz == and_sz
print "  op_size: %s/%s (%s)" % (shr_sz, and_sz, "same" if is_same_op_size else "DIFFERENT")
is_optimizable = False
if is_same_op_size:
    try: # only eval integer, no #define like 'SR_M' (cpp did this, else some headers are missing).
        len_v = long(len_s.strip("UL"), 0)
        low_bits = 0
        while (len_v & (1 << low_bits)):
            low_bits += 1
        print "  low_bits:", low_bits, "(value: 0x%x)" % ((1 << low_bits) - 1)
        print "  len: 0x%x" % len_v
        is_optimizable = ((1 << low_bits) - 1) == len_v # check low_bits
        print "  len_bits %s= low_bits" % ("=" if is_optimizable else "!")
        print "  candidate", "IS" if is_optimizable else "is NOT", "optimizable"
        coccinelle.extract_fn = "tcg_gen_extract_" + and_sz
    except:
        print "  ERROR (check included headers?)"
    cocci.include_match(is_optimizable)
print

@replacement depends on verify_len@
identifier match.ret, match.arg;
constant match.ofs, match.len;
identifier match.shr_fn;
identifier match.and_fn;
position match.shr_p;
position match.and_p;
identifier verify_len.extract_fn;
@@
-shr_fn@shr_p(ret, arg, ofs);
-and_fn@and_p(ret, ret, len);
+extract_fn(ret, arg, ofs, len);
