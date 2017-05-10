// optimize TCG using extract op
//
// Copyright: (C) 2017 Philippe Mathieu-Daudé. GPLv2.
// Confidence: High
// Options: --macro-file scripts/cocci-macro-file.h
@@
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
