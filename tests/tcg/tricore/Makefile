AS := tricore-as
LD := tricore-ld
HOST_CC = gcc

LDFLAGS = -Tlink.ld
ASFLAGS =

SIM = ../../../tricore-softmmu/qemu-system-tricore
SIMFLAGS = -M tricore_testboard -nographic -kernel

TESTCASES += test_abs.tst
TESTCASES += test_bmerge.tst
TESTCASES += test_clz.tst
TESTCASES += test_dvstep.tst
TESTCASES += test_fadd.tst
TESTCASES += test_fmul.tst
TESTCASES += test_ftoi.tst
TESTCASES += test_madd.tst
TESTCASES += test_msub.tst

all: build

%.pS: %.S
	$(HOST_CC) -E -o $@ $<

%.o: %.pS
	$(AS) $(ASFLAGS) -o $@ $<

%.tst: %.o link.ld
	$(LD) $(LDFLAGS) $< -o $@

build: $(TESTCASES)

check: $(addprefix run-, $(TESTCASES))

run-%.tst: %.tst
	$(SIM) $(SIMFLAGS) ./$<

clean:
	$(RM) -fr $(TESTCASES) linker.ld
