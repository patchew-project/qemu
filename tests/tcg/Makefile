-include ../../config-host.mak
-include $(SRC_PATH)/rules.mak

$(call set-vpath, $(SRC_PATH)/tests/tcg)

QEMU=../../i386-linux-user/qemu-i386
QEMU_X86_64=../../x86_64-linux-user/qemu-x86_64
CC_X86_64=$(CC_I386) -m64

QEMU_INCLUDES += -I../..
CFLAGS=-Wall -O2 -g -fno-strict-aliasing
#CFLAGS+=-msse2
LDFLAGS=

# TODO: automatically detect ARM and MIPS compilers, and run those too

# runcom maps page 0, so it requires root privileges
# also, pi_10.com runs indefinitely

# native i386 compilers sometimes are not biarch.  assume cross-compilers are
ifneq ($(ARCH),i386)
I386_TESTS+=run-test-x86_64
endif

TESTS = test_path
ifneq ($(call find-in-path, $(CC_I386)),)
TESTS += $(I386_TESTS)
endif

all: $(patsubst %,run-%,$(TESTS))
test: all

# rules to run tests

.PHONY: $(patsubst %,run-%,$(TESTS))

run-%: %
	-$(QEMU) ./$*

run-test-x86_64: test-x86_64
	./test-x86_64 > test-x86_64.ref
	-$(QEMU_X86_64) test-x86_64 > test-x86_64.out
	@if diff -u test-x86_64.ref test-x86_64.out ; then echo "Auto Test OK"; fi

run-test_path: test_path
	./test_path

# rules to compile tests

test_path: test_path.o

test_path.o: test_path.c

test-x86_64: test-i386.c \
           test-i386.h test-i386-shift.h test-i386-muldiv.h
	$(CC_X86_64) $(QEMU_INCLUDES) $(CFLAGS) $(LDFLAGS) -o $@ $(<D)/test-i386.c -lm

# speed test
sha1-i386: sha1.c
	$(CC_I386) $(CFLAGS) $(LDFLAGS) -o $@ $<

sha1: sha1.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

speed: sha1 sha1-i386
	time ./sha1
	time $(QEMU) ./sha1-i386

# arm test
hello-arm: hello-arm.o
	arm-linux-ld -o $@ $<

hello-arm.o: hello-arm.c
	arm-linux-gcc -Wall -g -O2 -c -o $@ $<

test-arm-iwmmxt: test-arm-iwmmxt.s
	cpp < $< | arm-linux-gnu-gcc -Wall -static -march=iwmmxt -mabi=aapcs -x assembler - -o $@

# MIPS test
hello-mips: hello-mips.c
	mips-linux-gnu-gcc -nostdlib -static -mno-abicalls -fno-PIC -mabi=32 -Wall -Wextra -g -O2 -o $@ $<

hello-mipsel: hello-mips.c
	mipsel-linux-gnu-gcc -nostdlib -static -mno-abicalls -fno-PIC -mabi=32 -Wall -Wextra -g -O2 -o $@ $<

# testsuite for the CRIS port.
test-cris:
	$(MAKE) -C cris check

# testsuite for the LM32 port.
test-lm32:
	$(MAKE) -C lm32 check

clean:
	rm -f *~ *.o test-i386.out test-i386.ref \
           test-x86_64.log test-x86_64.ref qruncom $(TESTS)
