// replace exit(0) by exit(EXIT_SUCCESS)
//         exit(1) by exit(EXIT_FAILURE)

@@
@@
(
- exit(0)
+ exit(EXIT_SUCCESS)
|
- exit(1)
+ exit(EXIT_FAILURE)
)
