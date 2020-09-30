; check_mpu.S
;
; Tests for MPUv3: Memory protection unit v3.
; If the test fails, check the end of this file for how to troubleshoot.

  .include "macros.inc"
  .include "mpu.inc"
  .include "mmu.inc"

;;;;;;;;;;;;;;;;;;;;;;;;;;; Test checking routines ;;;;;;;;;;;;;;;;;;;;;;;;;;

; Test case counter
.data
test_nr:
  .word 0x0

; Increment the test counter and set (Z,N,C,V) to (0,0,0,0).
.macro prep_test_case
  ld    r13, [test_nr]
  add_s r13, r13, 1       ; increase test case counter
  st    r13, [test_nr]
  add.f 0, 0, 1           ; (Z, N, C, V) = (0, 0, 0, 0)
.endm

; macro:     auxreg_write_read
; input:     reg      - the register we are talking about
;            write    - value to write
;            read     - value expected to be read
; regs used: r11, r12
; example:   auxreg_write_read mpuen, 0xffffffff, 0x400001f8
;
; description:
;   Not always, "write" and "read" values are the same. This true about
;   the registers who have reserved bits or read as zero in user mode,
;   etc.
;   Be careful, what is the result of you writing to to "reg". It may
;   have consequences like enabling page protection or so.
.macro auxreg_write_read reg, write, read
  mov   r11, \write
  sr    r11, [\reg]
  ; using a different register to reduce the chande of false equality
  lr    r12, [\reg]
  cmp   r12, \read
  bne   @fail
.endm

  start

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; check the MPU_BUILD
test_00:
  .equ VERSION   , 0x03
  .equ NR_REGIONS, 0x08
  .equ MPU_BCR_REF, (NR_REGIONS << 8) | VERSION
  lr    r0, [mpu_build]
  cmp   r0, MPU_BCR_REF
  bne   @fail

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; All of the registers should be accessible in kernel mode
; this test (check_mpu) is based on 8 regions.
test_01:
  prep_test_case
  ; mpuen : momentarily enabled with full access
  ; when read, only relevant bits must be set.
  auxreg_write_read mpuen   , 0xffffffff, 0x400001f8
  ; disable mpu at once
  mpu_disable
  auxreg_write_read mpurdb0 , 0xffffffff, 0xffffffe1
  auxreg_write_read mpurdp0 , 0xffffffff, 0x00000ffb
  auxreg_write_read mpurdb1 , 0xffffffff, 0xffffffe1
  auxreg_write_read mpurdp1 , 0xffffffff, 0x00000ffb
  auxreg_write_read mpurdb2 , 0xffffffff, 0xffffffe1
  auxreg_write_read mpurdp2 , 0xffffffff, 0x00000ffb
  auxreg_write_read mpurdb3 , 0xffffffff, 0xffffffe1
  auxreg_write_read mpurdp3 , 0xffffffff, 0x00000ffb
  auxreg_write_read mpurdb4 , 0xffffffff, 0xffffffe1
  auxreg_write_read mpurdp4 , 0xffffffff, 0x00000ffb
  auxreg_write_read mpurdb5 , 0xffffffff, 0xffffffe1
  auxreg_write_read mpurdp5 , 0xffffffff, 0x00000ffb
  auxreg_write_read mpurdb6 , 0xffffffff, 0xffffffe1
  auxreg_write_read mpurdp6 , 0xffffffff, 0x00000ffb
  auxreg_write_read mpurdb7 , 0xffffffff, 0xffffffe1
  auxreg_write_read mpurdp7 , 0xffffffff, 0x00000ffb

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; None of the registers should be accessible in user mode
test_02:
  prep_test_case
  mpu_reset
  ; prep the exception for the end
  lr    r0, [mpuic]    ; don't care for mpu_ecr value
  mpu_set_except_params mpu_ecr  = r0                  , \
                        ecr      = PRIVILEGE_VIOLATION , \
                        efa      = @test_02_user_space+4, \
                        eret     = @test_02_user_space+4, \
                        continue = @test_02_end
  enter_user_mode @test_02_user_space
test_02_user_space:
  add   r0, r0, r0           ; some filler to make a basic block
  ; accessing MPU registers in user mode is not allowed
  lr    r0, [mpu_build]
  b     @fail                ; an exception must have been raised
test_02_end:
  ; Fall through

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Running with -global cpu.has-mpu=false or reading a region register
; which is higher than the configured number of regions causes an
; instuction error: ecr=0x020000
test_03:
  prep_test_case
  mpu_reset
  ; prep the exception for 'lr'ing a region that does not exist
  lr    r0, [mpuic]    ; don't care for mpu_ecr value
  mpu_set_except_params mpu_ecr  = r0                     , \
                        ecr      = ILLEGAL_INSTRUCTION    , \
                        efa      = @test_03_illegal_lr_rgn, \
                        eret     = @test_03_illegal_lr_rgn, \
                        continue = @test_03_cont
test_03_illegal_lr_rgn:
  lr    r1, [mpurdb15]
  b     @fail                ; exception must have been raised
test_03_cont:
  ; prep the exception for 'sr'ing a region that does not exist
  lr    r0, [mpuic]    ; don't care for mpu_ecr value
  mpu_set_except_params mpu_ecr  = r0                     , \
                        ecr      = ILLEGAL_INSTRUCTION    , \
                        efa      = @test_03_illegal_sr_rgn, \
                        eret     = @test_03_illegal_sr_rgn, \
                        continue = @test_03_end
test_03_illegal_sr_rgn:
  sr    r1, [mpurdp8]
  b     @fail                ; an exception must have been raised
test_03_end:
  ; Fall through

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Verifying the KR permission for region 1.
; Checking if "read" is OK and "write" raises an exception.
test_04:
  .equ MEM_ADDR04  , 0x16000
  .equ DATA04      , 0x1337
  .equ MPU_ECR_W_R1, MPU_ECR_WRITE | 1
  prep_test_case
  mpu_reset
  mpu_add_base    mpurdb1, MEM_ADDR04
  mpu_add_region  mpurdp1, REG_MPU_EN_KR, MPU_SIZE_32B
  mpu_write_data  DATA04, MEM_ADDR04
  mpu_enable
  ; read permission check
  mpu_verify_data DATA04, MEM_ADDR04
  ; write permission check
  mpu_set_except_params mpu_ecr  = MPU_ECR_W_R1            , \
                        ecr      = PROTV_WRITE_MPU         , \
                        efa      = MEM_ADDR04              , \
                        eret     = @test_04_illegal_store+4, \
                        continue = @test_04_end
test_04_illegal_store:
  add   r0, r0, r0           ; filler; so exception happens in...
  st    r1, [MEM_ADDR04]     ; ...the middle of a translation block
  b     @fail                ; an exception must have been raised
test_04_end:
  ; Fall through

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Having 2 small regions next to each other: one with write permission
; and the other with read permission. Check if permissions are respected
; accordingly. This tests how MPU sets QEmu's internal TLB and if it is
; able to set the TLB's entry size correctly.
test_05:
  .equ MEM_ADDR05, 0x16024     ; 4 bytes above the multiple of 32
  .equ DATA05    , 0xbabe
  prep_test_case
  mpu_reset
  mpu_add_base    mpurdb0, MEM_ADDR05   ; effective address would be 0x4020
  mpu_add_region  mpurdp0, REG_MPU_EN_KW, MPU_SIZE_32B
  mpu_add_base    mpurdb1, MEM_ADDR05+32; effective address would be 0x4040
  mpu_add_region  mpurdp1, REG_MPU_EN_KR, MPU_SIZE_32B
  mpu_write_data  DATA05, MEM_ADDR05+32 ; write to 0x4044 (region1)
  ; let the fun begin
  mpu_enable
  mpu_verify_data DATA05, MEM_ADDR05+32
  st    r7, [MEM_ADDR05]       ; write bogus data (region 0)
  ; now time for some exception
  mpu_set_except_params mpu_ecr  = MPU_ECR_W_R1          , \
                        ecr      = PROTV_WRITE_MPU       , \
                        efa      = MEM_ADDR05+32         , \
                        eret     = @test_05_illegal_store, \
                        continue = @test_05_end
test_05_illegal_store:
  st    r7, [MEM_ADDR05+32]    ; this shouldn't be allowed
  b     @fail                  ; an exception must have been raised
test_05_end:
  ; Fall through

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Update a region's permission and size to check if they are taken
; into account.
test_06:
  .equ MEM_ADDR06,   0x30000
  .equ MPU_ECR_R_R3, MPU_ECR_READ | 3
  prep_test_case
  mpu_reset
  mpu_add_base    mpurdb3, MEM_ADDR06
  mpu_add_region  mpurdp3, REG_MPU_EN_KR, MPU_SIZE_64B
  mpu_enable
  ld r7, [MEM_ADDR06+32]    ; this should be allowed
  ; changing permission (deliberately mpu is not disabled)
  mpu_add_region  mpurdp3, REG_MPU_EN_KE, MPU_SIZE_64B ; update (KR -> KE)
  ; prep for exception
  mpu_set_except_params mpu_ecr  = MPU_ECR_R_R3         , \
                        ecr      = PROTV_READ_MPU       , \
                        efa      = MEM_ADDR06+32        , \
                        eret     = @test_06_illegal_read, \
                        continue = @test_06_change_size
test_06_illegal_read:
  ld    r7, [MEM_ADDR06+32]    ; this is not allowed anymore
  b     @fail                  ; an exception must have been raised
test_06_change_size:
  ; changing size (deliberately mpu is not disabled)
  mpu_add_region  mpurdp3, REG_MPU_EN_KE, MPU_SIZE_32B ; update (64 -> 32)
  mpu_enable
  ld    r7, [MEM_ADDR06+32]    ; this is allowed again (+32 is in def. region)

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Check a permission that has only execute permission.
; The read should not be possible.
test_07:
  .equ NOP_OPCODE, 0x7000264a
  .equ JR1_OPCODE, 0x00402020
  .equ CODE_CAVE07, 0x40000
  .equ MPU_ECR_R_R0, MPU_ECR_READ | 0
  prep_test_case
  mpu_reset
  mpu_add_base    mpurdb0, CODE_CAVE07
  mpu_add_region  mpurdp0, REG_MPU_EN_KE, MPU_SIZE_32B
  mov   r0, NOP_OPCODE
  mov   r1, @test_07_rest
  mov   r2, JR1_OPCODE
  st    r0, [CODE_CAVE07]    ; nop
  st    r2, [CODE_CAVE07+4]  ; j [r1]
  st    r0, [CODE_CAVE07+8]  ; nop
  mpu_enable
  ; let's take a leap of faith
  j     CODE_CAVE07

test_07_rest:
  ; wow, if we just came back, let's raise hell
  mpu_set_except_params mpu_ecr  = MPU_ECR_R_R0         , \
                        ecr      = PROTV_READ_MPU       , \
                        efa      = CODE_CAVE07+4        , \
                        eret     = @test_07_illegal_read, \
                        continue = @test_07_end
test_07_illegal_read:
  ld    r7, [CODE_CAVE07+4]    ; this shouldn't be allowed
  b     @fail                  ; an exception must have been raised
test_07_end:
  ; Fall through

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; One region to rule them all
; 1) We are testing a very big region here.
; 2) Moreover we change its permission and size in the middle
test_08:
  .equ MEM_ADDR08   , 0x00000000
  .equ BIG_ADDR08   , 0x7FFFFFE0
  .equ MPU_ECR_W_R7 , MPU_ECR_WRITE | 7
  .equ MPU_ECR_R_DEF, MPU_ECR_READ | 0xFF
  .equ DATA08_1     , 0x34fa                ; random magic
  .equ DATA08_2     , 0x987afb              ; random magic
  prep_test_case
  mpu_reset
  ; planting the data
  mpu_write_data  DATA08_1, BIG_ADDR08
  ; a 4 gigabyte region with read and execute permissions
  mpu_add_base    mpurdb7, MEM_ADDR08
  mpu_add_region  mpurdp7, REG_MPU_EN_KR | REG_MPU_EN_KE , MPU_SIZE_4G
  ; prepping exception (must be before enable, otherwise no write access)
  mpu_set_except_params mpu_ecr  = MPU_ECR_W_R7              , \
                        ecr      = PROTV_WRITE_MPU           , \
                        efa      = BIG_ADDR08                , \
                        eret     = @test_08_illegal_write    , \
                        continue = @test_08_change_permission, \
  ; default region with only write permission
  mpu_enable REG_MPU_EN_KW
  ; checking read (BIG_ADDR08) and exec (current instruction) permissions
  mpu_verify_data DATA08_1, BIG_ADDR08
test_08_illegal_write:
  st    r7, [BIG_ADDR08]       ; no write is allowed
  b     @fail                  ; an exception must have been raised
test_08_change_permission:
  ; change permission _and_ size
  mpu_add_region  mpurdp7, REG_MPU_EN_FULL_ACCESS , MPU_SIZE_2G
  ; now there should be no problem in writing either
  mpu_write_data  DATA08_2, BIG_ADDR08
  mpu_verify_data DATA08_2, BIG_ADDR08
  ; prepping second exception: default region has no read access
  mpu_set_except_params mpu_ecr  = MPU_ECR_R_DEF            , \
                        ecr      = PROTV_READ_MPU           , \
                        efa      = BIG_ADDR08+0xF0          , \
                        eret     = @test_08_illegal_def_read, \
                        continue = @test_08_end
test_08_illegal_def_read:
  ld    r7, [BIG_ADDR08+0xF0]  ; this is default region now and not sanctioned
  b     @fail                  ; an exception must have been raised
test_08_end:
  ; Fall through

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; A user cannot have kernel permissions, but a kernel inherits granted
; user permissions as well.
test_09:
  .equ MEM_ADDR09_1, 0x60000
  .equ MEM_ADDR09_2, 0x62000              ; 8k after
  .equ MPU_ECR_W_R6, MPU_ECR_WRITE | 6
  .equ DATA09      , 0x89091              ; another random data from beyond
  prep_test_case
  mpu_reset
  ; a region for user to write
  mpu_add_base   mpurdb5, MEM_ADDR09_1
  mpu_add_region mpurdp5, REG_MPU_EN_UW, MPU_SIZE_8K
  ; a region only for kernel
  mpu_add_base   mpurdb6, MEM_ADDR09_2
  mpu_add_region mpurdp6, REG_MPU_EN_KR | REG_MPU_EN_KW, MPU_SIZE_8K
  ; prep the exception for the end
  mpu_set_except_params mpu_ecr  = MPU_ECR_W_R6             , \
                        ecr      = PROTV_WRITE_MPU          , \
                        efa      = MEM_ADDR09_2             , \
                        eret     = @test_09_user_space+8    , \
                        continue = @test_09_rest_kernel_mode
  ; let's have at it
  mpu_enable REG_MPU_EN_UE | REG_MPU_EN_KR
  enter_user_mode @test_09_user_space
test_09_user_space:
  st    r7, [MEM_ADDR09_2-4]   ; write to the end of user region
  st    r7, [MEM_ADDR09_2]     ; uh-oh: causing trouble
  b     @fail                  ; an exception must have been raised
test_09_rest_kernel_mode:
  ; a simple write and verify chore in kernel mode
  mpu_write_data  DATA09, MEM_ADDR09_2+64
  mpu_verify_data DATA09, MEM_ADDR09_2+64
  ; also writing to user region because of implied write access
  mpu_write_data  DATA09, MEM_ADDR09_1+64
  mpu_disable       ; else we cannot verify (no read access)
  mpu_verify_data DATA09, MEM_ADDR09_1+64

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; A region with only user read access should not be fetchable.
test_10:
  .equ CODE_CAVE10 , 0x100000
  .equ DATA10      , 0x010101
  .equ MPU_ECR_E_R4, MPU_ECR_FETCH | 4
  prep_test_case
  mpu_reset
  mpu_add_base   mpurdb4, CODE_CAVE10
  mpu_add_region mpurdp4, REG_MPU_EN_UR, MPU_SIZE_64K
  ; plant the data
  mpu_write_data DATA10, CODE_CAVE10
  ; prep the exception for the region being not executable
  mpu_set_except_params mpu_ecr  = MPU_ECR_E_R4   , \
                        ecr      = PROTV_FETCH_MPU, \
                        efa      = CODE_CAVE10    , \
                        eret     = CODE_CAVE10    , \
                        continue = @test_10_end
  mpu_enable
  enter_user_mode @test_10_user_space
test_10_user_space:
  mpu_verify_data DATA10, CODE_CAVE10  ; read must be OK
  j @CODE_CAVE10                       ; this one not
  b     @fail                  ; an exception must have been raised
test_10_end:
  ; Fall through

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; KE must be enough for raising exceptions.
; The tricky thing about this test is that it is allowing the
; parameters for the exceptions to be readable. As a result,
; the test assumes that there is 32 byte region that these
; parameters fit in AND it does not overlap with the exception
; routine itself.
test_11:
  .equ MEM_ADDR11, 0x900
  prep_test_case
  mpu_reset
  ; allowing exception parameters to be read
  mpu_add_base   mpurdb0, @mpu_ecr_ref
  mpu_add_region mpurdp0, REG_MPU_EN_KR, MPU_SIZE_32B
  ; prep for the exception
  mpu_set_except_params mpu_ecr  = MPU_ECR_R_DEF        , \
                        ecr      = PROTV_READ_MPU       , \
                        efa      = MEM_ADDR11           , \
                        eret     = @test_11_illegal_read, \
                        continue = @test_11_end
  mpu_enable REG_MPU_EN_KE
  add   r0, r0, r0           ; just a random guy making a difference
test_11_illegal_read:
  ld    r0, [MEM_ADDR11]
  b     @fail                ; an exception must have been raised
test_11_end:
  mpu_disable

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Double exception must raise Machine Check with memory management disabled.
; This test hangs in nSIM if MMU exists. Apparently, nSIM sets the halt flag
; if a MachineCheck is raised and there is MMU in the system. The  presence
; of MMU is necessary for test 14.
test_12:
  .equ MPU_ECR_E_DEF, MPU_ECR_FETCH | 0xFF
  prep_test_case
  mpu_reset
  ; enable MPU with no access whatsoever
  mpu_enable 0x0
test_12_doomed:
  add   r0, r0, r0
  lr    r0, [mpuen]
  cmp   r0, 0
  bne   @fail
  j     @test_12_end
  ; the machine check routine to be executed eventually
	.global	 EV_MachineCheck
	.type	   EV_MachineCheck, @function
	.align 4
EV_MachineCheck:
  lr   r0, [mpuen]
  cmp  r0, REG_MPU_EN_EN
  bne  @fail
  lr   r0, [mpuic]
  cmp  r0, MPU_ECR_E_DEF
  bne  @fail
  lr   r0, [ecr]
  cmp  r0, MACHINE_CHECK
  bne  @fail
  lr   r0, [eret]
  cmp  r0, @test_12_doomed
  bne  @fail
  lr   r1, [efa]
  cmp  r0, r1
  bne  @fail
  mpu_disable         ; disable MPU in a civilized way
  lr   r0, [erstatus] ; undo the mess:
  and  r0, r0, ~32    ; clear AE bit
  sr   r0, [erstatus] ; and
  rtie                ; return
test_12_end:
  ; Fall through

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Page size for the default region: best effort 8K, else 1 byte. You need
; to look into tracing to see if it is doing the right thing.
test_13:
  .equ TWO_PAGES_BEFORE , 0x7C000
  .equ ONE_PAGE_BEFORE  , 0x7E000
  .equ MEM_ADDR13_1     , 0x80000
  .equ SAME_PAGE_BETWEEN, 0x80050
  .equ MEM_ADDR13_2     , 0x80100
  .equ SAME_PAGE_AFTER  , 0x81000
  .equ ONE_PAGE_AFTER   , 0x82000
  .equ MPU_ECR_R_R1     , MPU_ECR_READ | 1
  prep_test_case
  mpu_reset
  mpu_add_base   mpurdb3, MEM_ADDR13_1        ; \
  mpu_add_region mpurdp3, 0x0, MPU_SIZE_32B   ;  | two black holes
  mpu_add_base   mpurdb1, MEM_ADDR13_2        ;  | alike regions
  mpu_add_region mpurdp1, 0x0, MPU_SIZE_32B   ; /
  ; your exception shall be your salvation
  mpu_set_except_params mpu_ecr  = MPU_ECR_R_R1         , \
                        ecr      = PROTV_READ_MPU       , \
                        efa      = MEM_ADDR13_2         , \
                        eret     = @test_13_illegal_read, \
                        continue = @test_13_end
  mpu_enable
  ld r0, [TWO_PAGES_BEFORE+0x1000]    ; must cache the page
  ld r0, [TWO_PAGES_BEFORE+0x1100]    ; reuse same information
  ld r0, [ONE_PAGE_BEFORE +0x1FFC]    ; oooh, just before the black hole
  ld r0, [ONE_PAGE_BEFORE +0x0500]    ; reuse from above
  ld r0, [SAME_PAGE_BETWEEN      ]    ; too narrow to cache the page
  ld r0, [SAME_PAGE_BETWEEN+0x10 ]    ; permissions must be totally checked
  ld r0, [SAME_PAGE_AFTER        ]    ; same page as the black holes
  ld r0, [SAME_PAGE_AFTER+0x10   ]    ; no caching must be used
  ld r0, [ONE_PAGE_AFTER         ]    ; this area is safe and ...
  ld r0, [ONE_PAGE_AFTER+0x04    ]    ; ...can be cached
test_13_illegal_read:
  ld r0, [MEM_ADDR13_2           ]    ; oops!
  b     @fail                         ; an exception must have been raised
test_13_end:
  ; Fall through

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; MMU and MPU may coexist but do not overlap.
; This test assumes an "rwe" access for exception routine checks and an "re"
; access for the page this test case is loaded in. If these two pages happen
; to be the same, e.g. previous tests are commented out, then things will
; get nasty, because the last attribute will be used for both.
test_14:
  .equ MMU_KRNL_RE , REG_PD1_KRNL_E | REG_PD1_KRNL_R
  .equ MMU_KRNL_RWE, REG_PD1_KRNL_E | REG_PD1_KRNL_W | REG_PD1_KRNL_R
  .equ MMU_VPN_GV  , REG_PD0_GLOBAL | REG_PD0_VALID
  .equ MEM_ADDR14  , 0x80000100          ; an address in MPU's interest
  ; creates an entry in TLB with given permissions.
  ; the translation is identical (virt = physical)
  .macro add_mmu_entry addr, permission
    mov r2, \addr
    and r2, r2, PAGE_NUMBER_MSK
    or  r3, r2, \permission  ; r3 holds physical address and permissoins
    or  r2, r2, MMU_VPN_GV   ; r2 is a global valid virtual address
    mmu_tlb_insert r2, r3    ; add entry for MMU
  .endm
  prep_test_case
  mpu_reset
  b     @test_14_after_align
  ; guarantee that current page won't be the same as @mp_ecr_ref's page
  .align 0x2000
test_14_after_align:
  ; add a read/write/execute permission for exception part page
  ; @mpu_ecr_ref and ProtV handler must be in the same page.
  add_mmu_entry @mpu_ecr_ref, MMU_KRNL_RWE
  ; add a read/write/execute permission for vector table.
  add_mmu_entry 0x0, MMU_KRNL_RWE
  ; add a read/execute permission for current page
  lr r1, [pc]
  add_mmu_entry r1, MMU_KRNL_RE
  ; exception for writing to the (2nd) MMU page
  lr    r0, [mpuic]          ; don't care for mpu_ecr value
  mpu_set_except_params mpu_ecr  = r0                    , \
                        ecr      = PROTV_WRITE_MMU       , \
                        efa      = r1                    , \
                        eret     = @test_14_illegal_write, \
                        continue = @test_14_mpu
  ; enable the guys
  mmu_enable                 ; enable MMU
  mpu_enable REG_MPU_EN_KW   ; enable MPU with kernel write access
  ; this is happening in MMU's territory
test_14_illegal_write:
  st    r0, [r1]             ; no write for this entry in TLB
  b     @fail                ; an exception must have been raised

test_14_mpu:
  add   r0, r0, r0           ; a happy camper
  st    r0, [MEM_ADDR14]     ; in MPU realm
  ; MPU exception now
  mpu_set_except_params mpu_ecr  = MPU_ECR_R_DEF        , \
                        ecr      = PROTV_READ_MPU       , \
                        efa      = MEM_ADDR14           , \
                        eret     = @test_14_illegal_read, \
                        continue = @test_14_end
test_14_illegal_read:
  ld    r0, [MEM_ADDR14]     ; uh-oh...
  b     @fail                ; an exception must have been raised
test_14_end:
  mpu_disable
  mmu_disable

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Two overlapping regions test. One is 2 pages long and the other is inside
; the second page of the first region:
;        ,----------.
;        | R2:rw-   | region nr 2 with read/write permission.
; page1  |          |
;        |          |
; .......|..........|.......
;        |          |
; page2  |,________.|
;        ||R1:r--  || region nr 1 with read only permission.
;        |`--------'| this region is inside region nr 2.
;        `----------'
; setup: R2 is 16kb with rw-
;        R1 is  4kb with r--
; write to the first page           --> must go ok.
; write to the first half of page 2 --> must go ok.
; write to R1                       --> expect an exception.
; in the end read from R1           --> must go ok.
test_15:
  .equ MEM_ADDR15_R2   , 0x150000
  .equ MEM_ADDR15_R2_P2, MEM_ADDR15_R2 + PAGE_SIZE
  .equ MEM_ADDR15_R1   , MEM_ADDR15_R2_P2 + PAGE_SIZE/2
  .equ DATA15_1        , 0x3ff0293f    ; random magic
  .equ DATA15_2        , DATA15_1+1
  .equ DATA15_3        , DATA15_1+2
  .equ MPU_ECR_W_R1, MPU_ECR_WRITE | 1
  prep_test_case
  mpu_reset
  mpu_add_base    mpurdb1, MEM_ADDR15_R1
  mpu_add_region  mpurdp1, REG_MPU_EN_KR, MPU_SIZE_4K
  mpu_add_base    mpurdb2, MEM_ADDR15_R2
  mpu_add_region  mpurdp2, REG_MPU_EN_KR|REG_MPU_EN_KW, MPU_SIZE_16K
  ; planting some data (for later read)
  mpu_write_data  DATA15_1, MEM_ADDR15_R1+24
  ; let the fun begin
  mpu_enable
  mpu_write_data  DATA15_2, MEM_ADDR15_R2+20
  mpu_verify_data DATA15_2, MEM_ADDR15_R2+20
  mpu_write_data  DATA15_3, MEM_ADDR15_R2+20+PAGE_SIZE
  mpu_verify_data DATA15_3, MEM_ADDR15_R2+20+PAGE_SIZE
  ; now time for some exception
  mpu_set_except_params mpu_ecr  = MPU_ECR_W_R1          , \
                        ecr      = PROTV_WRITE_MPU       , \
                        efa      = MEM_ADDR15_R1+24      , \
                        eret     = @test_15_illegal_store, \
                        continue = @test_15_cont
  st    r7, [MEM_ADDR15_R2_P2+32] ; write bogus data (region 2, page 2)
test_15_illegal_store:
  st    r7, [MEM_ADDR15_R1+24]    ; this shouldn't be allowed
  b     @fail                     ; an exception must have been raised
test_15_cont:
  mpu_verify_data DATA15_1, MEM_ADDR15_R1+24  ; this is allowed

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Another overlapping regions test. In previous one, a page (nr=2) was split
; among two regions.  in  this  test, the page  is contained inside  another
; region, which in return is inside yet another region:
;        ,----------.
; page1  | R5:r---  | region nr 5 with read only permission.
; .......|..........|.......
; page2  |          |
; .......|..........|.......
; page3  |          |
; .......|..........|.......
; page4  |          |
; .......|,________.|.......
; page5  ||R3:-w-  || region nr 3 with write only permission.
; .......||........||.......
; page6  ||        || this region is inside region nr 5.
; .......|`--------'|.......
; page7  |          |
; .......|..........|.......
; page8  |          |
;        `----------'
; setup: R3 is 16kb with -w-
;        R5 is 64kb with r--
; read from the fourth page  --> must go ok.
; read from page 7           --> must go ok.
; write to page 4            --> expect an exception.
; write to page 5            --> must go ok.
; read from page 6           --> expect an exception.
test_16:
  .equ MEM_ADDR16_R5   , 0x160000
  .equ MEM_ADDR16_R5_P4, MEM_ADDR16_R5 + 3*PAGE_SIZE
  .equ MEM_ADDR16_R5_P7, MEM_ADDR16_R5 + 6*PAGE_SIZE
  .equ MEM_ADDR16_R3   , MEM_ADDR16_R5 + 4*PAGE_SIZE
  .equ MEM_ADDR16_R3_P5, MEM_ADDR16_R3
  .equ MEM_ADDR16_R3_P6, MEM_ADDR16_R5 + 5*PAGE_SIZE
  .equ DATA16_1        , 0x93822093    ; random magic
  .equ DATA16_2        , DATA16_1+1
  .equ DATA16_3        , DATA16_1+2
  .equ MPU_ECR_R_R3, MPU_ECR_READ  | 3
  .equ MPU_ECR_W_R5, MPU_ECR_WRITE | 5
  prep_test_case
  mpu_reset
  mpu_add_base    mpurdb3, MEM_ADDR16_R3
  mpu_add_region  mpurdp3, REG_MPU_EN_KW, MPU_SIZE_16K
  mpu_add_base    mpurdb5, MEM_ADDR16_R5
  mpu_add_region  mpurdp5, REG_MPU_EN_KR, MPU_SIZE_64K
  ; planting some data (for later read)
  mpu_write_data  DATA16_1, MEM_ADDR16_R5_P4+24
  mpu_write_data  DATA16_3, MEM_ADDR16_R5_P7+24
  ; let the fun begin
  mpu_enable
  mpu_verify_data DATA16_1, MEM_ADDR16_R5_P4+24
  mpu_verify_data DATA16_3, MEM_ADDR16_R5_P7+24
  ; first exception because of writing in region 5
  mpu_set_except_params mpu_ecr  = MPU_ECR_W_R5          , \
                        ecr      = PROTV_WRITE_MPU       , \
                        efa      = MEM_ADDR16_R5_P4+24   , \
                        eret     = @test_16_illegal_store, \
                        continue = @test_16_cont
test_16_illegal_store:
  st    r7, [MEM_ADDR16_R5_P4+24] ; this shouldn't be allowed
  b     @fail                     ; an exception must have been raised
test_16_cont:
  mpu_write_data  DATA16_2, MEM_ADDR16_R3_P5+24 ;will be checked later
  ; second exception while reading in region 3
  mpu_set_except_params mpu_ecr  = MPU_ECR_R_R3         , \
                        ecr      = PROTV_READ_MPU       , \
                        efa      = MEM_ADDR16_R3_P6+24  , \
                        eret     = @test_16_illegal_read, \
                        continue = @test_16_end
test_16_illegal_read:
  ld    r7, [MEM_ADDR16_R3_P6+24] ; this shouldn't be allowed
  b     @fail                     ; an exception must have been raised
test_16_end:
  mpu_disable
  mpu_verify_data DATA16_2, MEM_ADDR16_R3_P5+24  ; check if written

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;; Reporting ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

valhalla:
  print "[PASS]"
  b @1f

; If a test fails, it jumps here. Although, for the sake of uniformity,
; the printed output does not say much about which test case failed,
; one can uncomment the print_number line below or set a breakpoint
; here to check the R0 register for the test case number.
fail:
  ld r0, [test_nr]
  print "[FAIL"
  print ":"
  print_number r0
  print "]"
1:
  print " MPUv3: Memory protection unit v3.\n"
  end
