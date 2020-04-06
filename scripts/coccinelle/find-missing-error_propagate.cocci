// Find places likely missing error-propagation code, but code is too
// complex for automatic transformation, so manual analysis is required.
//
// Copyright: (C) 2020 Philippe Mathieu-Daudé
// This work is licensed under the terms of the GNU GPLv2 or later.
//
// spatch \
//  --macro-file scripts/cocci-macro-file.h --include-headers \
//  --sp-file scripts/coccinelle/find-missing-error_propagate.cocci
//
// Inspired by https://www.mail-archive.com/qemu-devel@nongnu.org/msg691638.html


// First match two subsequent calls using local Error*
// in function provided a Error** argument
//
@discard_func_with_errp_argument@
typedef Error;
Error *local_err;
identifier func, errp, errfunc1, errfunc2;
@@
void func(..., Error **errp)
{
 <+...
 errfunc1(..., &local_err);
 ... when != local_err          // local_err is not used between the calls
 errfunc2(..., &local_err);
 ...+>
}


// Again, match two subsequent calls using local Error*
// but ignoring within functions provided a Error** argument
//
@manual depends on never discard_func_with_errp_argument@
Error *local_err;
identifier errfunc1, errfunc2;
position p;
@@
 errfunc1@p(..., &local_err);
 ... when != local_err
 errfunc2(..., &local_err);


// As it is likely too complex to transform, report the hit
//
@script:python@
f << manual.errfunc1;
p << manual.p;
@@
print("[[manual check required: "
      "error_propagate() might be missing in {}() {}:{}:{}]]".format(
            f, p[0].file, p[0].line, p[0].column))
