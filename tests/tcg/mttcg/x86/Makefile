GCC=gcc
GCCOPTS=-D_GNU_SOURCE -DFORCE_AFFINITY -Wall -std=gnu99 -fomit-frame-pointer -O2 -pthread
LINKOPTS=
SRC=\
 SAL.c\

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
