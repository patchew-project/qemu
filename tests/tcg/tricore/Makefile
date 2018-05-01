AS := tricore-as
LD := tricore-ld
HOST_CC = gcc

LDFLAGS = -Tlink.ld
ASFLAGS =

SIM = ../../../tricore-softmmu/qemu-system-tricore
SIMFLAGS = -M tricore_testboard -nographic -kernel

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
