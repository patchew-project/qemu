@@
expression list X;
@@

-error_report(X);
+error_report_exit(X);
-exit(
(
-1
|
-EXIT_FAILURE
)
-);
