Tests produced by litmus for architecture X86 on linux 

COMPILING
  with command 'make [-j N]' or 'sh comp.sh'

RUNNING ALL TESTS
  with command 'sh run.sh'. Test result on standard output.

RUNNING ONE TEST
  Tests are .exe files, for instance SAL.exe, run it by './SAL.exe'

RUNNING OPTIONS
  Main options to the run.sh script and to .exe files:
  -v     be verbose (can be repeated).
  -a <n> number of (logical) processors available, default 0.
      The default value of 0 means that .exe files attempt
      to infer the actual number of logical threads.
  -s <n> one run operates on arrays of size <n>, default 100000.
  -r <n> number of runs, default 10.

  For more options see for instance './SAL.exe -help' and litmus documentation
  <http://diy.inria.fr/doc/litmus.html>
