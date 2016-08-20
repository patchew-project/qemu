GCC=aarch64-linux-gnu-gcc
GCCOPTS=-D_GNU_SOURCE -DFORCE_AFFINITY -Wall -std=gnu99 -O2 -pthread -static
LINKOPTS=
SRC=\
 ARMARM00.c\
 ARMARM01.c\
 ARMARM02.c\
 ARMARM03.c\
 ARMARM04+BIS.c\
 ARMARM04.c\
 ARMARM04+TER.c\
 ARMARM05.c\
 ARMARM06+AP+AA.c\
 ARMARM06+AP+AP.c\
 ARMARM06.c\
 ARMARM07+SAL.c\

EXE=$(SRC:.c=.exe)
T=$(SRC:.c=.t)

all: $(EXE) $(T)

clean:
	/bin/rm -f *.o *.s *.t *.exe *~

cleansource:
	/bin/rm -f *.o *.c *.h *.s *~

affinity.o: affinity.c
	$(GCC) $(GCCOPTS) -O2 -c affinity.c

outs.o: outs.c
	$(GCC) $(GCCOPTS) -O2 -c outs.c

utils.o: utils.c
	$(GCC) $(GCCOPTS) -O2 -c utils.c

litmus_rand.o: litmus_rand.c
	$(GCC) $(GCCOPTS) -O2 -c litmus_rand.c

UTILS=affinity.o outs.o utils.o litmus_rand.o

%.exe:%.s $(UTILS)
	$(GCC) $(GCCOPTS) $(LINKOPTS) -o $@ $(UTILS) $<

%.s:%.c
	$(GCC) $(GCCOPTS) -S $<

%.t:%.s
	awk -f show.awk $< > $@

tests: all
	./run.sh
