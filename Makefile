# Makefile for the QEMU source directory.

ifneq ($(words $(subst :, ,$(CURDIR))), 1)
  $(error main directory cannot contain spaces nor colons)
endif

SRCPATH_GOALS = docker docker-% vm-% ctags TAGS cscope dist clean distclean recurse
.PHONY: all clean distclean ctags TAGS cscope dist help
.NOTPARALLEL: %

all:
clean:; @:
distclean::; @:

ctags:
	rm -f tags
	find . -name '*.[hc]' -exec ctags --append {} +

TAGS:
	rm -f TAGS
	find . -name '*.[hc]' -exec etags --append {} +

cscope:
	rm -f ./cscope.*
	find . -name "*.[chsS]" -print | sed -e 's,^\./,,' > "./cscope.files"
	cscope -b -i./cscope.files

VERSION = $(shell cat VERSION)
.PHONY: dist
dist: qemu-$(VERSION).tar.bz2
qemu-%.tar.bz2:
	./scripts/make-release . "$(patsubst qemu-%.tar.bz2,%,$@)"

SRC_PATH = .
include tests/docker/Makefile.include
include tests/vm/Makefile.include

print-help = @printf "  %-30s - %s\\n" "$1" "$2"

# Fake in-tree build support

ifeq ($(wildcard build/auto-created-by-configure),)
all: help
	@exit 1

help:
	@echo 'This is not a build directory.'
	@echo 'Please call configure to build QEMU.'
	@echo  ''
	@echo  'Generic targets:'
	$(call print-help,ctags/TAGS,Generate tags file for editors)
	$(call print-help,cscope,Generate cscope index)
	$(call print-help,dist,Build a distributable tarball)
	@echo  ''
	@echo  'Test targets:'
	$(call print-help,docker,Help about targets running tests inside containers)
	$(call print-help,vm-help,Help about targets running tests inside VM)

else
$(filter-out $(SRCPATH_GOALS), all $(MAKECMDGOALS)): recurse
.PHONY: $(MAKECMDGOALS)
recurse:
	@echo 'changing dir to build for $(MAKE) "$(MAKECMDGOALS)"...'
	@$(MAKE) -C build -f Makefile $(MAKECMDGOALS)

distclean::
	rm -rf build
endif
