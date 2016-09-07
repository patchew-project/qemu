@@
expression list X;
@@

-error_report(X);
-abort();
+error_report_abort(X);

@@
expression list X;
@@

-error_setg(&error_abort, X);
+error_report_abort(X);
