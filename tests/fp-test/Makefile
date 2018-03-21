BUILD_DIR=$(CURDIR)/../..

include ../../config-host.mak
include $(SRC_PATH)/rules.mak

$(call set-vpath, $(SRC_PATH)/tests/fp-test $(SRC_PATH)/fpu)

QEMU_INCLUDES += -I../..
QEMU_INCLUDES += -I$(SRC_PATH)/fpu
# work around TARGET_* poisoning
QEMU_CFLAGS += -DHW_POISON_H

IBMFP := ibm-fptests.zip

OBJS := fp-test$(EXESUF)

WHITELIST_FILES := whitelist.txt whitelist-tininess-after.txt

all: $(OBJS) ibm $(WHITELIST_FILES)

ibm:
	wget -nv -O $(IBMFP) http://www.haifa.il.ibm.com/projects/verification/fpgen/download/test_suite.zip
	mkdir -p $@
	unzip $(IBMFP) -d $@
	rm -rf $(IBMFP)

# XXX: upload this to a qemu server, or just commit it.
$(WHITELIST_FILES):
	wget -nv -O $@ http://www.cs.columbia.edu/~cota/qemu/fpbench-$@

fp-test$(EXESUF): fp-test.o softfloat.o hostfloat.o

clean:
	rm -f *.o *.d $(OBJS)
