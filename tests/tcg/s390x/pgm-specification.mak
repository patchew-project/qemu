# SPDX-License-Identifier: GPL-2.0-or-later
PGM_SPECIFICATION_TESTS = \
	br-odd \
	cgrl-unaligned \
	clrl-unaligned \
	crl-unaligned \
	ex-odd \
	lgrl-unaligned \
	llgfrl-unaligned \
	lpswe-unaligned \
	lrl-unaligned \
	stgrl-unaligned \
	strl-unaligned
$(PGM_SPECIFICATION_TESTS) : asm-const.h pgm-specification.inc
TESTS += $(PGM_SPECIFICATION_TESTS)
