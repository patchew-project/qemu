#!/bin/sh
#
# indent wrapper script, with args to format
# source code according to qemu coding style.
#
indent	--ignore-profile	\
	--k-and-r-style		\
	--line-length 80	\
	--indent-level 4	\
	--no-tabs		\
	"$@"
