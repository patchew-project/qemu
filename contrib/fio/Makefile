# -*- Mode: makefile -*-

BUILD_DIR?=$(CURDIR)/../..

include ../../config-host.mak
include $(SRC_PATH)/rules.mak

$(call set-vpath, $(SRC_PATH):$(SRC_PATH)/contrib/fio:$(CURDIR))

PROGS=qemu.so

all: $(PROGS)
# Dummy command so that make thinks it has done something
	@true

QEMU_CFLAGS += -I$(BUILD_DIR) -Wno-error -Wno-redundant-decls
LIBS += $(LIBS_TOOLS)

# We need two fio header files, but we don't want to include
# fio's config-host.h because its #defined symbols conflict
# with QEMU's own config-host.h.  In general we do not want to
# have -I$(FIO_PATH) while compiling fio.o because the possible
# conflicts are a mess.
#
# optgroup.h is tame and we can just copy it to the build directory,
# but fio.h includes a lot of other header files, from both fio
# itself and the system.  Therefore we preprocess it so that
# fio's headers are merged into a single file, fio-qemu.h, while
# system headers are left as #include directives.  Because the
# preprocessing step removes all preprocessor conditionals,
# there is no dependency left on fio's config-host.h file.
#
# While at it, we hack some symbols that conflict with QEMU's,
# by prefixing them with a "FIO_" or "fio_" namespace.
fio.o: fio-qemu.h fio-optgroup-qemu.h

fio-optgroup-qemu.h:
	cp $(FIO_PATH)/optgroup.h $@

FIO_HACK_SYMBOLS=sed -e 's/\<JSON_/FIO_&/' -e 's/\<cpu_to_/fio_&/'
fio-qemu.h:
	$(CC) -dD -dI -C -E -o - -I$(FIO_PATH) $(QEMU_CFLAGS) \
            -include $(FIO_PATH)/config-host.h $(FIO_PATH)/fio.h | \
            awk -f $(SRC_PATH)/contrib/fio/uninclude.awk | \
            $(FIO_HACK_SYMBOLS) > $@

include $(SRC_PATH)/Makefile.objs
dummy := $(call unnest-vars,../.., \
               block-obj-y \
               block-obj-m \
               crypto-obj-y \
               qom-obj-y \
               io-obj-y)
obj-y := fio.o $(block-obj-y) $(crypto-obj-y) $(io-obj-y) $(qom-obj-y)

dummy := $(shell mkdir -p $(dir $(obj-y)))
qemu.so: $(obj-y) ../../libqemuutil.a ../../libqemustub.a
	$(call LINK, $^)

clean: clean-target
	rm -f *.a $(filter-out ../../%, $(obj-y))
	rm -f $(shell find . -name '*.[od]')
