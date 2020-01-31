// Use ERRP_AUTO_PROPAGATE (see include/qapi/error.h)
//
// Copyright (c) 2020 Virtuozzo International GmbH.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// Usage example:
// spatch --sp-file scripts/coccinelle/auto-propagated-errp.cocci \
//  --macro-file scripts/cocci-macro-file.h --in-place --no-show-diff \
//  blockdev-nbd.c qemu-nbd.c {block/nbd*,nbd/*,include/block/nbd*}.[hc]

@rule0@
// Add invocation to errp-functions where necessary
// We should skip functions with "Error *const *errp"
// parameter, but how to do it with coccinelle?
// I don't know, so, I skip them by function name regex.
// It's safe: if we did not skip some functions with
// "Error *const *errp", ERRP_AUTO_PROPAGATE invocation
// will fail to compile, because of const violation.
identifier fn !~ "error_append_.*_hint";
identifier local_err, ERRP;
@@

 fn(..., Error **ERRP, ...)
 {
+   ERRP_AUTO_PROPAGATE();
    <+...
        when != ERRP_AUTO_PROPAGATE();
(
    error_append_hint(ERRP, ...);
|
    error_prepend(ERRP, ...);
|
    Error *local_err = NULL;
)
    ...+>
 }

@@
// Switch unusual (Error **) parameter names to errp
// (this is necessary to use ERRP_AUTO_PROPAGATE).
identifier rule0.fn;
identifier rule0.ERRP != errp;
@@

 fn(...,
-   Error **ERRP
+   Error **errp
    ,...)
 {
     <...
-    ERRP
+    errp
     ...>
 }

@rule1@
// We want to patch error propagation in functions regardless of
// whether the function already uses ERRP_AUTO_PROPAGATE prior to
// applying rule0, hence this one does not inherit from it.
identifier fn !~ "error_append_.*_hint";
identifier local_err;
symbol errp;
@@

 fn(..., Error **errp, ...)
 {
     <...
-    Error *local_err = NULL;
     ...>
 }

@@
// Handle pattern with goto, otherwise we'll finish up
// with labels at function end which will not compile.
identifier rule1.fn, rule1.local_err;
identifier OUT;
@@

 fn(...)
 {
     <...
-    goto OUT;
+    return;
     ...>
- OUT:
-    error_propagate(errp, local_err);
 }

@@
identifier rule1.fn, rule1.local_err;
expression list args; // to reindent error_propagate_prepend
@@

 fn(...)
 {
     <...
(
-    error_free(local_err);
-    local_err = NULL;
+    error_free_errp(errp);
|
-    error_free(local_err);
+    error_free_errp(errp);
|
-    error_report_err(local_err);
+    error_report_errp(errp);
|
-    warn_report_err(local_err);
+    warn_report_errp(errp);
|
-    error_propagate_prepend(errp, local_err, args);
+    error_prepend(errp, args);
|
-    error_propagate(errp, local_err);
)
     ...>
 }

@@
identifier rule1.fn, rule1.local_err;
@@

 fn(...)
 {
     <...
(
-    &local_err
+    errp
|
-    local_err
+    *errp
)
     ...>
 }

@@
identifier rule1.fn;
@@

 fn(...)
 {
     <...
- *errp != NULL
+ *errp
     ...>
 }
