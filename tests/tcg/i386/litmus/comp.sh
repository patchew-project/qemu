GCC=gcc
GCCOPTS="-D_GNU_SOURCE -DFORCE_AFFINITY -Wall -std=gnu99 -fomit-frame-pointer -O2 -pthread"
LINKOPTS=""
/bin/rm -f *.exe *.s
$GCC $GCCOPTS -O2 -c affinity.c
$GCC $GCCOPTS -O2 -c outs.c
$GCC $GCCOPTS -O2 -c utils.c
$GCC $GCCOPTS -O2 -c litmus_rand.c
$GCC $GCCOPTS $LINKOPTS -o SAL.exe affinity.o outs.o utils.o litmus_rand.o SAL.c
$GCC $GCCOPTS -S SAL.c && awk -f show.awk SAL.s > SAL.t && /bin/rm SAL.s
