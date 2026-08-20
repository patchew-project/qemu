# SPDX-License-Identifier: GPL-2.0-or-later
#
# Bare-metal reproducers for pseries (sPAPR) machine behaviour.
#
# These are tiny PowerPC payloads booted with -kernel on -M pseries.  They
# talk to the hypervisor interface directly -- hypercalls, CRQ, SRP -- so
# they exercise QEMU's sPAPR implementation with no guest operating system
# anywhere in the picture.  That makes a failure attributable to one
# function in QEMU rather than to something a kernel did on the way there.
#
# The built payloads are committed so the tests can be run without a
# PowerPC cross toolchain, following tests/multiboot.  They are small:
# linking with -N keeps the loader from page-aligning the load segment,
# which would otherwise pad a 584-byte payload out to 65 KiB of zeros.
#
# Rebuild with:
#     make CROSS=powerpc64-linux-gnu-
#
# and confirm the committed binary still matches its source with:
#     make check-reproducible

CROSS ?= powerpc64-linux-gnu-
AS     = $(CROSS)as
LD     = $(CROSS)ld
STRIP  = $(CROSS)strip

ASFLAGS = -many
LDFLAGS = -N -T link.ld

PAYLOADS = vscsibig
ELFS     = $(PAYLOADS:%=%.elf)

all: $(ELFS)

%.o: %.S
	$(AS) $(ASFLAGS) -o $@ $<

%.elf: %.o link.ld
	$(LD) $(LDFLAGS) -o $@ $<
	$(STRIP) $@

# The payloads are position-fixed and self-contained, so rebuilding from
# the same source must produce the same bytes.  If this fails, the
# committed ELF and the committed .S have drifted apart and one of them is
# lying about what was tested.
check-reproducible: $(ELFS)
	@for p in $(PAYLOADS); do \
	    cp $$p.elf $$p.elf.committed; \
	    $(MAKE) --no-print-directory -B $$p.elf CROSS=$(CROSS) >/dev/null; \
	    if cmp -s $$p.elf $$p.elf.committed; then \
	        echo "  $$p.elf: reproducible"; \
	    else \
	        echo "  $$p.elf: DIFFERS from the committed binary"; \
	        mv $$p.elf.committed $$p.elf; exit 1; \
	    fi; \
	    mv $$p.elf.committed $$p.elf; \
	done

clean:
	rm -f *.o *.elf.committed vscsidisk.raw *.monitor.txt

.PHONY: all clean check-reproducible
