.include "macros.inc"
.include "mmu.inc"

; courtesy of mmu.inc
.extern PAGE_NUMBER_MSK
.extern REG_PD0_GLOBAL
.extern REG_PD0_VALID
.extern REG_PD1_KRNL_W

; test data
; making an entry for the TLB
;
; ,------------------------------------.
; | VPN(VA), G=1, V=1 | PPN(PHY), Wk=1 |
; `------------------------------------'
; where:
; VPN(VA) is the virtual page number of logical address
; G is the global bit
; V is the validity bit
; PPN(PHY) is the physical page number
; Wk is the write permission in kernel mode

; obviously, the offsets in both addresses must be the same
.equ VIRT_ADR , 0x13371334     ; the virtual address; word aligned
.equ PHYS_ADR , 0x73311334     ; the physical address > 0x7FFFFFFF
.equ MAGICDATA, 0x00BADB07     ; the test value to write and verify
.equ PD0_VPN  , (VIRT_ADR & PAGE_NUMBER_MSK)
.equ PD1_PPN  , (PHYS_ADR & PAGE_NUMBER_MSK)
.equ PD0_BITS , (PD0_VPN | REG_PD0_GLOBAL | REG_PD0_VALID)
.equ PD1_BITS , (PD1_PPN | REG_PD1_KRNL_R)
.equ INT_VECT_ADDRESS, 0x80000000 ; physical address for IVT

start

; use physicall address range for handling exceptions (ivt)
mov r0, INT_VECT_ADDRESS
sr  r0, [REG_IVT_BASE]

; initialize the data in physical address
mov r0, MAGICDATA
st  r0, [PHYS_ADR]

mmu_enable

; read from the mapped virtual address
mov r2, 0
ld.aw  r1, [r2, VIRT_ADR]

mmu_disable

; with mmu disabled, read from physical address and
; verify that it is the same  as the  value written
; to the mapped virtual address earlier
cmp r0, r1           ; r0 contains the MAGICDATA
beq @goodboy

print "nope, still no MMU!\n"
j   @adios

goodboy:
print "Yay, you got the MMU right :)\n"

adios:
end

.align 4
.global EV_TLBMissD
.type EV_TLBMissD, @function
EV_TLBMissD:
mmu_tlb_insert PD0_BITS, PD1_BITS
rtie
