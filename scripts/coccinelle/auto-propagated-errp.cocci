@rule0@
// Add invocation to errp-functions where necessary
identifier fn, local_err;
symbol errp;
@@

 fn(..., Error **errp, ...)
 {
+   ERRP_AUTO_PROPAGATE();
    <+...
(
    error_append_hint(errp, ...);
|
    error_prepend(errp, ...);
|
    Error *local_err = NULL;
)
    ...+>
 }

@@
// Drop doubled invocation
identifier rule0.fn;
@@

 fn(...)
{
-   ERRP_AUTO_PROPAGATE();
    ERRP_AUTO_PROPAGATE();
    ...
}

@rule1@
// Drop local_err
identifier fn, local_err;
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
identifier rule1.fn;
identifier rule1.local_err;
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
identifier rule1.fn;
identifier rule1.local_err;
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
-    error_propagate_prepend(errp, local_err,
+    error_prepend(errp,
                              ...);
|
-    error_propagate(errp, local_err);
)
     ...>
 }

@@
identifier rule1.fn;
identifier rule1.local_err;
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
symbol errp;
@@

- *errp != NULL
+ *errp
