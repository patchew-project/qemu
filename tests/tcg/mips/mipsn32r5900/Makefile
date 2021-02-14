-include ../../config-host.mak

CROSS=mips64r5900el-unknown-linux-gnu-

SIM=qemu-mipsn32el
SIM_FLAGS=-cpu R5900

CC      = $(CROSS)gcc
CFLAGS  = -Wall -mabi=n32 -march=r5900 -static

TESTCASES = dmult.tst

all: $(TESTCASES)

%.tst: %.c
	$(CC) $(CFLAGS) $< -o $@

check: $(TESTCASES)
	@for case in $(TESTCASES); do \
        echo $(SIM) $(SIM_FLAGS) ./$$case;\
        $(SIM) $(SIM_FLAGS) ./$$case; \
	done

clean:
	$(RM) -rf $(TESTCASES)
