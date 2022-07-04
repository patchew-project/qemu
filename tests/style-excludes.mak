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
