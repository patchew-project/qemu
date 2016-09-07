@@
expression list X;
@@

-error_report(X);
+error_report_fatal(X);
-exit(
(
-1
|
-EXIT_FAILURE
)
-);

@@
expression list X;
@@

-error_setg(&error_fatal, X);
+error_report_fatal(X);
