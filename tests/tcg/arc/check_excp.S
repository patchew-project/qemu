	.include "macros.inc"

	start

	test_name TRAP_1
	trap_s 0
	print "[PASS] TRAP_1:1\n"
	trap_s 1
	print "[PASS] TRAP_1:2\n"
	end

	.align 4
	.global EV_Trap
	.type EV_Trap, @function
EV_SWI:
EV_Trap:
	rtie
