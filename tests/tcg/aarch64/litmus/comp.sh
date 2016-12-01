GCC=gcc
GCCOPTS="-D_GNU_SOURCE -DFORCE_AFFINITY -Wall -std=gnu99 -O2 -pthread"
LINKOPTS=""
/bin/rm -f *.exe *.s
$GCC $GCCOPTS -O2 -c affinity.c
$GCC $GCCOPTS -O2 -c outs.c
$GCC $GCCOPTS -O2 -c utils.c
$GCC $GCCOPTS -O2 -c litmus_rand.c
$GCC $GCCOPTS $LINKOPTS -o ARMARM00.exe affinity.o outs.o utils.o litmus_rand.o ARMARM00.c
$GCC $GCCOPTS -S ARMARM00.c && awk -f show.awk ARMARM00.s > ARMARM00.t && /bin/rm ARMARM00.s
$GCC $GCCOPTS $LINKOPTS -o ARMARM01.exe affinity.o outs.o utils.o litmus_rand.o ARMARM01.c
$GCC $GCCOPTS -S ARMARM01.c && awk -f show.awk ARMARM01.s > ARMARM01.t && /bin/rm ARMARM01.s
$GCC $GCCOPTS $LINKOPTS -o ARMARM02.exe affinity.o outs.o utils.o litmus_rand.o ARMARM02.c
$GCC $GCCOPTS -S ARMARM02.c && awk -f show.awk ARMARM02.s > ARMARM02.t && /bin/rm ARMARM02.s
$GCC $GCCOPTS $LINKOPTS -o ARMARM03.exe affinity.o outs.o utils.o litmus_rand.o ARMARM03.c
$GCC $GCCOPTS -S ARMARM03.c && awk -f show.awk ARMARM03.s > ARMARM03.t && /bin/rm ARMARM03.s
$GCC $GCCOPTS $LINKOPTS -o ARMARM04+BIS.exe affinity.o outs.o utils.o litmus_rand.o ARMARM04+BIS.c
$GCC $GCCOPTS -S ARMARM04+BIS.c && awk -f show.awk ARMARM04+BIS.s > ARMARM04+BIS.t && /bin/rm ARMARM04+BIS.s
$GCC $GCCOPTS $LINKOPTS -o ARMARM04.exe affinity.o outs.o utils.o litmus_rand.o ARMARM04.c
$GCC $GCCOPTS -S ARMARM04.c && awk -f show.awk ARMARM04.s > ARMARM04.t && /bin/rm ARMARM04.s
$GCC $GCCOPTS $LINKOPTS -o ARMARM04+TER.exe affinity.o outs.o utils.o litmus_rand.o ARMARM04+TER.c
$GCC $GCCOPTS -S ARMARM04+TER.c && awk -f show.awk ARMARM04+TER.s > ARMARM04+TER.t && /bin/rm ARMARM04+TER.s
$GCC $GCCOPTS $LINKOPTS -o ARMARM05.exe affinity.o outs.o utils.o litmus_rand.o ARMARM05.c
$GCC $GCCOPTS -S ARMARM05.c && awk -f show.awk ARMARM05.s > ARMARM05.t && /bin/rm ARMARM05.s
$GCC $GCCOPTS $LINKOPTS -o ARMARM06+AP+AA.exe affinity.o outs.o utils.o litmus_rand.o ARMARM06+AP+AA.c
$GCC $GCCOPTS -S ARMARM06+AP+AA.c && awk -f show.awk ARMARM06+AP+AA.s > ARMARM06+AP+AA.t && /bin/rm ARMARM06+AP+AA.s
$GCC $GCCOPTS $LINKOPTS -o ARMARM06+AP+AP.exe affinity.o outs.o utils.o litmus_rand.o ARMARM06+AP+AP.c
$GCC $GCCOPTS -S ARMARM06+AP+AP.c && awk -f show.awk ARMARM06+AP+AP.s > ARMARM06+AP+AP.t && /bin/rm ARMARM06+AP+AP.s
$GCC $GCCOPTS $LINKOPTS -o ARMARM06.exe affinity.o outs.o utils.o litmus_rand.o ARMARM06.c
$GCC $GCCOPTS -S ARMARM06.c && awk -f show.awk ARMARM06.s > ARMARM06.t && /bin/rm ARMARM06.s
