.include "macros.inc"

	start

	test_name NORM_1
	norm r2, 0x0
	check_r2	0x1f

	test_name NORM_2
	norm r2, 0x1
	check_r2	0x1e

	test_name NORM_3
	norm r2, 0x1fffffff
	check_r2	0x02

	test_name NORM_4
	norm r2, 0x3fffffff
	check_r2	0x01

	test_name NORM_5
	norm r2, 0x7fffffff
	check_r2	0x00

	test_name NORM_6
	norm r2, 0x80000000
	check_r2	0x00

	test_name NORM_8
	norm r2, 0xc0000000
	check_r2	0x01

	test_name NORM_9
	norm r2, 0xe0000000
	check_r2	0x02

	test_name NORM_10
	norm r2, 0xffffffff
	check_r2	0x1f
	end
