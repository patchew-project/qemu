#!/bin/awk -f
# SPDX-License-Identifier: GPL-2.0-or-later
# arm-gen-cpu-sysregs-header.awk: arm64 sysreg header include generator
#
# Usage: awk -f arm-gen-cpu-sysregs-header.awk $LINUX_PATH/arch/arm64/tools/sysreg

BEGIN {
    print "/* SPDX-License-Identifier: GPL-2.0-or-later */"
    print "/* GENERATED FILE, DO NOT EDIT */"
    print "/* use arm-gen-cpu-sysregs-header.awk to regenerate */"
} END {
    print ""
}

# skip blank lines and comment lines
/^$/ { next }
/^[\t ]*#/ { next }

/^Sysreg\t/ || /^Sysreg /{

	reg = $2
	op0 = $3
	op1 = $4
	crn = $5
	crm = $6
	op2 = $7

	if (op0 == 3 && (op1==0 || op1==1 || op1==3) && crn==0 && (crm>=0 && crm<=7) && (op2>=0 && op2<=7)) {
	    print "DEF("reg", "op0", "op1", "crn", "crm", "op2")"
	}
	next
}

{
	/* skip all other lines */
	next
}
