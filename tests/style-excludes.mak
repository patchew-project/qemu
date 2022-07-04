# -*- makefile -*-
#
# Filenames that should be excluded from specific
# style checks performed by style.mak

exclude_file_name_regexp--sc_prohibit_doubled_word = \
	disas/sparc\.c \
	hw/char/terminal3270\.c \
	include/crypto/afsplit\.h \
	qemu-options\.hx \
	scripts/checkpatch\.pl \
	target/s390x/tcg/insn-data\.def \
	pc-bios/slof\.bin \
	tests/qemu-iotests/142(\.out)? \
	tests/qtest/arm-cpu-features\.c \
	ui/cursor\.c

exclude_file_name_regexp--sc_c_file_osdep_h = \
	contrib/plugins/.* \
	linux-user/(mips64|x86_64)/(signal|cpu_loop)\.c \
	pc-bios/.* \
	scripts/coverity-scan/model\.c \
	scripts/xen-detect\.c \
	subprojects/.* \
	target/hexagon/(gen_semantics|gen_dectree_import)\.c \
	target/s390x/gen-features\.c \
	tests/migration/s390x/a-b-bios\.c \
	tests/multiboot/.* \
	tests/plugin/.* \
	tests/tcg/.* \
	tests/uefi-test-tools/.* \
	tests/unit/test-rcu-(simpleq|slist|tailq)\.c \
	tools/ebpf/rss.bpf.c
