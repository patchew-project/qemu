include ../../../../config-host.mak
include ../../../config-target.mak
include $(SRC_PATH)/rules.mak

MOD_SRC_PATH = $(SRC_PATH)/hypertrace/guest/linux-module

LINUX_BUILD_PATH = /lib/modules/$(shell uname -r)/build

vpath % $(MOD_SRC_PATH)


all: Kbuild
	$(MAKE) -C $(LINUX_BUILD_PATH) M=$(shell pwd) \
	  MOD_SRC_PATH=$(MOD_SRC_PATH) \
	  modules

clean: Kbuild
	$(MAKE) -C $(LINUX_BUILD_PATH) M=$(shell pwd) clean
	rm -f $<

Kbuild: $(MOD_SRC_PATH)/Kbuild.in Makefile
	rm -f $@
	cp $< $@
