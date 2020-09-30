.include "macros.inc"

.equ PHYSICAL_ADDRESS_START, 0x80000000
.equ MMU_ENABLE_FLAG       , 0x80000000

start

; use physical address range for handling exceptions (ivt)
mov r0, PHYSICAL_ADDRESS_START
sr  r0, [int_vector_base]

# enable mmu
mov   r3, MMU_ENABLE_FLAG
sr    r3, [pid]
xor_s r3, r3, r3

; write to some virtual address range in a delay slot
mov r2, 0x1000
mov r1, @check
j_s.d [r1]        # let's enjoy the code after delay slot is executed.
st  r0, [r2]      # oh, oh: exception!

# this line should not be executed
add_s r3, r3, 1

check:
brgt r3, 0, @fail
print "[ OK]"
b @rest
fail:
print "[NOK]"

rest:
print " Exception in a delay slot.\n"

end

	.align 4
	.global EV_TLBMissD
	.type EV_TLBMissD, @function
EV_TLBMissD:
  # disable mmu
  sr r3, [pid]
	rtie
