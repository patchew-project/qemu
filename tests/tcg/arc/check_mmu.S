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
.equ PD1_BITS , (PD1_PPN | REG_PD1_KRNL_W)

start

mmu_enable

; insert into table: VA 0x13371337 (Global) --> PHY: 0x73311337 (RW kernel)
mmu_tlb_insert PD0_BITS, PD1_BITS

; write to the mapped virtual address
mov r0, MAGICDATA
st  r0, [VIRT_ADR]

mmu_disable

; with mmu disabled, read from physical address and
; verify that it is the same  as the  value written
; to the mapped virtual address earlier
ld  r1, [PHYS_ADR]
cmp r0, r1           ; r0 contains the MAGICDATA
beq @goodboy

print "nope, still no MMU!\n"
j   @adios

goodboy:
print "Yay, you got the MMU right :)\n"

adios:
end
