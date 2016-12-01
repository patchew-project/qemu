date
LITMUSOPTS="${@:-$LITMUSOPTS}"
SLEEP=0
if [ ! -f ARMARM00.no ]; then
cat <<'EOF'
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% Results for aarch64.tests/HAND/ARMARM00.litmus %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
AArch64 ARMARM00
"PodWWPL RfeLA PodRRAP Fre"

{0:X1=x; 0:X3=y; 1:X1=y; 1:X3=x; 1:X2=-1;}

 P0           | P1           ;
 MOV W0,#1    | LDAR W0,[X1] ;
 STR W0,[X1]  | CMP W0,#1    ;
 MOV W2,#1    | B.NE Exit1   ;
 STLR W2,[X3] | LDR W2,[X3]  ;
              | Exit1:       ;

~exists (1:X0=1 /\ 1:X2=0)
Generated assembler
EOF
cat ARMARM00.t
$QEMU ./ARMARM00.exe -q $LITMUSOPTS
ret=$?
if [ $ret -eq 1 ]; then
    echo "FAILED";
    exit 1;
fi
fi
sleep $SLEEP

if [ ! -f ARMARM01.no ]; then
cat <<'EOF'
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% Results for aarch64.tests/HAND/ARMARM01.litmus %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
AArch64 ARMARM01
"PodWWPL RfeLP DpAddrdR Fre"

{0:X1=x; 0:X3=y; 1:X1=y; 1:X4=x; 1:X3=-1;}

 P0           | P1                  ;
 MOV W0,#1    | LDR W0,[X1]         ;
 STR W0,[X1]  | CMP W0,#1           ;
 MOV W2,#1    | B.NE Exit1          ;
 STLR W2,[X3] | EOR W2,W0,W0        ;
              | LDR W3,[X4,W2,SXTW] ;
              | Exit1:              ;

~exists (1:X0=1 /\ 1:X3=0)
Generated assembler
EOF
cat ARMARM01.t
$QEMU ./ARMARM01.exe -q $LITMUSOPTS
ret=$?
if [ $ret -eq 1 ]; then
    echo "FAILED";
    exit 1;
fi
fi
sleep $SLEEP

if [ ! -f ARMARM02.no ]; then
cat <<'EOF'
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% Results for aarch64.tests/HAND/ARMARM02.litmus %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
AArch64 ARMARM02

{0:X1=x; 0:X3=y; 1:X1=y; 1:X4=x; 1:X3=-1; 2:X1=y; 2:X4=x; 2:X3=-1;}

 P0           | P1                  | P2                  ;
 MOV W0,#1    | LDR W0,[X1]         | LDR W0,[X1]         ;
 STR W0,[X1]  | CMP W0,#1           | CMP W0,#1           ;
 MOV W2,#1    | B.NE Exit1          | B.NE Exit2          ;
 STLR W2,[X3] | EOR W2,W0,W0        | EOR W2,W0,W0        ;
              | LDR W3,[X4,W2,SXTW] | LDR W3,[X4,W2,SXTW] ;
              | Exit1:              | Exit2:              ;

~exists (1:X0=1 /\ 1:X3=0 \/ 2:X0=1 /\ 2:X3=0)
Generated assembler
EOF
cat ARMARM02.t
$QEMU ./ARMARM02.exe -q $LITMUSOPTS
ret=$?
if [ $ret -eq 1 ]; then
    echo "FAILED";
    exit 1;
fi
fi
sleep $SLEEP

if [ ! -f ARMARM03.no ]; then
cat <<'EOF'
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% Results for aarch64.tests/HAND/ARMARM03.litmus %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
AArch64 ARMARM03
"PodWWPL RfeLP DpAddrdR Fre"

{y=z; z=-1; 0:X1=x; 0:X3=y; 1:X3=y; 1:X9=-1;}

 P0           | P1          ;
 MOV W0,#1    | LDR X0,[X3] ;
 STR W0,[X1]  | LDR W9,[X0] ;
 STLR X1,[X3] |             ;

~exists (1:X0=x /\ 1:X9=0)
Generated assembler
EOF
cat ARMARM03.t
$QEMU ./ARMARM03.exe -q $LITMUSOPTS
ret=$?
if [ $ret -eq 1 ]; then
    echo "FAILED";
    exit 1;
fi
fi
sleep $SLEEP

if [ ! -f ARMARM04+BIS.no ]; then
cat <<'EOF'
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% Results for aarch64.tests/HAND/ARMARM04+BIS.litmus %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
AArch64 ARMARM04+BIS
"RfeLA PodRWAP Rfe DpAddrdRPA FreAL"

{0:X1=x; 1:X1=x; 1:X3=y; 2:X1=y; 2:X4=x; 2:X3=-1;}

 P0           | P1           | P2                ;
 MOV W0,#1    | LDAR W0,[X1] | LDR W0,[X1]       ;
 STLR W0,[X1] | CMP W0,#1    | CMP W0,#1         ;
              | B.NE Exit1   | B.NE Exit2        ;
              | MOV W2,#1    | EOR W2,W0,W0      ;
              | STR W2,[X3]  | ADD X5,X4,W2,SXTW ;
              | Exit1:       | LDR W3,[X5]       ;
              |              | Exit2:            ;

exists (1:X0=1 /\ 2:X0=1 /\ 2:X3=0)
Generated assembler
EOF
cat ARMARM04+BIS.t
$QEMU ./ARMARM04+BIS.exe -q $LITMUSOPTS
ret=$?
if [ $ret -eq 1 ]; then
    echo "FAILED";
    exit 1;
fi
fi
sleep $SLEEP

if [ ! -f ARMARM04.no ]; then
cat <<'EOF'
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% Results for aarch64.tests/HAND/ARMARM04.litmus %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
AArch64 ARMARM04
"RfeLA PodRWAP Rfe DpAddrdRPA FreAL"

{0:X1=x; 1:X1=x; 1:X3=y; 2:X1=y; 2:X4=x; 2:X3=-1;}

 P0           | P1           | P2                ;
 MOV W0,#1    | LDAR W0,[X1] | LDR W0,[X1]       ;
 STLR W0,[X1] | CMP W0,#1    | CMP W0,#1         ;
              | B.NE Exit1   | B.NE Exit2        ;
              | MOV W2,#1    | EOR W2,W0,W0      ;
              | STR W2,[X3]  | ADD X5,X4,W2,SXTW ;
              | Exit1:       | LDAR W3,[X5]      ;
              |              | Exit2:            ;

~exists (1:X0=1 /\ 2:X0=1 /\ 2:X3=0)
Generated assembler
EOF
cat ARMARM04.t
$QEMU ./ARMARM04.exe -q $LITMUSOPTS
ret=$?
if [ $ret -eq 1 ]; then
    echo "FAILED";
    exit 1;
fi
fi
sleep $SLEEP

if [ ! -f ARMARM04+TER.no ]; then
cat <<'EOF'
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% Results for aarch64.tests/HAND/ARMARM04+TER.litmus %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
AArch64 ARMARM04+TER
"RfeLA PodRWAP Rfe PodRRPA FreAL"

{0:X1=x; 1:X1=x; 1:X3=y; 2:X1=y; 2:X3=x;}

 P0           | P1           | P2           ;
 MOV W0,#1    | LDAR W0,[X1] | LDR W0,[X1]  ;
 STLR W0,[X1] | MOV W2,#1    | LDAR W2,[X3] ;
              | STR W2,[X3]  |              ;

exists (1:X0=1 /\ 2:X0=1 /\ 2:X2=0)
Generated assembler
EOF
cat ARMARM04+TER.t
$QEMU ./ARMARM04+TER.exe -q $LITMUSOPTS
ret=$?
if [ $ret -eq 1 ]; then
    echo "FAILED";
    exit 1;
fi
fi
sleep $SLEEP

if [ ! -f ARMARM05.no ]; then
cat <<'EOF'
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% Results for aarch64.tests/HAND/ARMARM05.litmus %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
AArch64 ARMARM05
"Rfe PodRWPL RfeLP DpAddrdR Fre"

{0:X1=x; 1:X1=x; 1:X3=y; 2:X1=y; 2:X4=x; 2:X3=-1;}

 P0          | P1           | P2                  ;
 MOV W0,#1   | LDR W0,[X1]  | LDR W0,[X1]         ;
 STR W0,[X1] | CMP W0,#1    | CMP W0,#1           ;
             | B.NE Exit1   | B.NE Exit2          ;
             | MOV W2,#1    | EOR W2,W0,W0        ;
             | STLR W2,[X3] | LDR W3,[X4,W2,SXTW] ;
             | Exit1:       | Exit2:              ;

~exists (1:X0=1 /\ 2:X0=1 /\ 2:X3=0)
Generated assembler
EOF
cat ARMARM05.t
$QEMU ./ARMARM05.exe -q $LITMUSOPTS
ret=$?
if [ $ret -eq 1 ]; then
    echo "FAILED";
    exit 1;
fi
fi
sleep $SLEEP

if [ ! -f ARMARM06+AP+AA.no ]; then
cat <<'EOF'
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% Results for aarch64.tests/HAND/ARMARM06+AP+AA.litmus %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
AArch64 ARMARM06+AP+AA
"RfeLA PodRRAP FrePL RfeLA PodRRAA FreAL"

{0:X1=x; 1:X1=x; 1:X3=y; 2:X1=y; 3:X1=y; 3:X3=x;}

 P0           | P1           | P2           | P3           ;
 MOV W0,#1    | LDAR W0,[X1] | MOV W0,#1    | LDAR W0,[X1] ;
 STLR W0,[X1] | LDR W2,[X3]  | STLR W0,[X1] | LDAR W2,[X3] ;

exists (1:X0=1 /\ 1:X2=0 /\ 3:X0=1 /\ 3:X2=0)
Generated assembler
EOF
cat ARMARM06+AP+AA.t
$QEMU ./ARMARM06+AP+AA.exe -q $LITMUSOPTS
ret=$?
if [ $ret -eq 1 ]; then
    echo "FAILED";
    exit 1;
fi
fi
sleep $SLEEP

if [ ! -f ARMARM06+AP+AP.no ]; then
cat <<'EOF'
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% Results for aarch64.tests/HAND/ARMARM06+AP+AP.litmus %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
AArch64 ARMARM06+AP+AP
"RfeLA PodRRAP FrePL RfeLA PodRRAP FrePL"

{0:X1=x; 1:X1=x; 1:X3=y; 2:X1=y; 3:X1=y; 3:X3=x;}

 P0           | P1           | P2           | P3           ;
 MOV W0,#1    | LDAR W0,[X1] | MOV W0,#1    | LDAR W0,[X1] ;
 STLR W0,[X1] | LDR W2,[X3]  | STLR W0,[X1] | LDR W2,[X3]  ;

exists (1:X0=1 /\ 1:X2=0 /\ 3:X0=1 /\ 3:X2=0)
Generated assembler
EOF
cat ARMARM06+AP+AP.t
$QEMU ./ARMARM06+AP+AP.exe -q $LITMUSOPTS
ret=$?
if [ $ret -eq 1 ]; then
    echo "FAILED";
    exit 1;
fi
fi
sleep $SLEEP

if [ ! -f ARMARM06.no ]; then
cat <<'EOF'
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% Results for aarch64.tests/HAND/ARMARM06.litmus %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
AArch64 ARMARM06
"RfeLA PodRRAA FreAL RfeLA PodRRAA FreAL"

{0:X1=x; 1:X1=x; 1:X3=y; 2:X1=y; 3:X1=y; 3:X3=x;}

 P0           | P1           | P2           | P3           ;
 MOV W0,#1    | LDAR W0,[X1] | MOV W0,#1    | LDAR W0,[X1] ;
 STLR W0,[X1] | LDAR W2,[X3] | STLR W0,[X1] | LDAR W2,[X3] ;

~exists (1:X0=1 /\ 1:X2=0 /\ 3:X0=1 /\ 3:X2=0)
Generated assembler
EOF
cat ARMARM06.t
$QEMU ./ARMARM06.exe -q $LITMUSOPTS
ret=$?
if [ $ret -eq 1 ]; then
    echo "FAILED";
    exit 1;
fi
fi
sleep $SLEEP

if [ ! -f ARMARM07+SAL.no ]; then
cat <<'EOF'
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% Results for aarch64.tests/HAND/ARMARM07+SAL.litmus %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
AArch64 SAL

{0:X1=x; 0:X2=y; 1:X1=x; 1:X2=y;}

 P0          | P1          ;
 MOV W0,#1   | MOV W0,#1   ;
 STR W0,[X1] | STR W0,[X2] ;
 DMB SY      | DMB SY      ;
 LDR X3,[X2] | LDR X3,[X1] ;

~exists (0:X3=0 /\ 1:X3=0)
Generated assembler
EOF
cat ARMARM07+SAL.t
$QEMU ./ARMARM07+SAL.exe -q $LITMUSOPTS
ret=$?
if [ $ret -eq 1 ]; then
    echo "FAILED";
    exit 1;
fi
fi
sleep $SLEEP

cat <<'EOF'
Revision exported, version 7.22
Command line: ./litmus -exit true -o run.armarm -mach overdrive01.cfg aarch64.tests/HAND/ARMARM00.litmus aarch64.tests/HAND/ARMARM01.litmus aarch64.tests/HAND/ARMARM02.litmus aarch64.tests/HAND/ARMARM03.litmus aarch64.tests/HAND/ARMARM04+BIS.litmus aarch64.tests/HAND/ARMARM04.litmus aarch64.tests/HAND/ARMARM04+TER.litmus aarch64.tests/HAND/ARMARM05.litmus aarch64.tests/HAND/ARMARM06+AP+AA.litmus aarch64.tests/HAND/ARMARM06+AP+AP.litmus aarch64.tests/HAND/ARMARM06.litmus aarch64.tests/HAND/ARMARM07+SAL.litmus
Parameters
#define SIZE_OF_TEST 100000
#define NUMBER_OF_RUN 10
#define AVAIL 0
#define STRIDE 1
#define MAX_LOOP 0
/* gcc options: -D_GNU_SOURCE -DFORCE_AFFINITY -Wall -std=gnu99 -O2 -pthread */
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
