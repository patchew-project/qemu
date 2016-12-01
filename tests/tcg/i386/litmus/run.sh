date
LITMUSOPTS="${@:-$LITMUSOPTS}"
SLEEP=0
if [ ! -f SAL.no ]; then
cat <<'EOF'
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% Results for x86.tests/SAL.litmus %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
X86 SAL
"Fre PodWR Fre PodWR"

{x=0; y=0;}

 P0          | P1          ;
 MOV [x],$1  | MOV [y],$1  ;
 MFENCE      | MFENCE      ;
 MOV EAX,[y] | MOV EAX,[x] ;

~exists (0:EAX=0 /\ 1:EAX=0)
Generated assembler
EOF
cat SAL.t
$QEMU ./SAL.exe -q $LITMUSOPTS
ret=$?;
if [ $ret -eq 1 ]; then
    echo "FAILED";
    exit $ret;
fi
fi
sleep $SLEEP

cat <<'EOF'
Revision exported, version 7.22
Command line: ../litmus-7.22/litmus -exit true -mach ../alex_litmus/overdrive01 -o run.x86 x86.tests/SAL.litmus
Parameters
#define SIZE_OF_TEST 100000
#define NUMBER_OF_RUN 10
#define AVAIL 0
#define STRIDE 1
#define MAX_LOOP 0
/* gcc options: -D_GNU_SOURCE -DFORCE_AFFINITY -Wall -std=gnu99 -fomit-frame-pointer -O2 -pthread */
/* barrier: user */
/* launch: changing */
/* affinity: incr0 */
/* alloc: dynamic */
/* memory: direct */
/* stride: 1 */
/* safer: write */
/* preload: random */
/* speedcheck: no */
/* proc used: 0 */
EOF
head -1 comp.sh
echo "LITMUSOPTS=$LITMUSOPTS"
date
