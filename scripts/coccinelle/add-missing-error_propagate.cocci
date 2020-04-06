// Add missing error-propagation code where caller provide a Error* argument
//
// Copyright: (C) 2020 Philippe Mathieu-Daudé
// This work is licensed under the terms of the GNU GPLv2 or later.
//
// spatch \
//  --macro-file scripts/cocci-macro-file.h --include-headers \
//  --sp-file scripts/coccinelle/add-missing-error_propagate.cocci \
//  --keep-comments --in-place
//
// Inspired by https://www.mail-archive.com/qemu-devel@nongnu.org/msg691638.html


@ add_missing_error_propagate @
typedef Error;
Error *local_err;
identifier func, errp, errfunc1, errfunc2;
@@
func(..., Error **errp)
{
    <...
    errfunc1(..., &local_err);
+   if (local_err) {
+       error_propagate(errp, local_err);
+       return;
+   }
    ... when != local_err
    errfunc2(..., &local_err);
    ...>
}
