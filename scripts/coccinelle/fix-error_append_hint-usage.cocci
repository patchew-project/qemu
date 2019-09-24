@rule0@
// Add invocation to errp-functions
identifier fn;
@@

 fn(..., Error **errp, ...)
 {
+   ERRP_FUNCTION_BEGIN();
    <+...
    error_append_hint(errp, ...);
    ...+>
 }

@@
// Drop doubled invocation
identifier rule0.fn;
@@

 fn(...)
{
    ERRP_FUNCTION_BEGIN();
-   ERRP_FUNCTION_BEGIN();
    ...
}

