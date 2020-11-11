; MPU related defines and macros

  .equ REG_MPU_EN_EN , 0x40000000  ; enable bit
  .equ REG_MPU_EN_KR , 0b100000000 ; kernel read
  .equ REG_MPU_EN_KW , 0b010000000 ; kernel write
  .equ REG_MPU_EN_KE , 0b001000000 ; kernel execute
  .equ REG_MPU_EN_UR , 0b000100000 ; user read
  .equ REG_MPU_EN_UW , 0b000010000 ; user write
  .equ REG_MPU_EN_UE , 0b000001000 ; user execute
  .equ REG_MPU_EN_MSK, REG_MPU_EN_EN | REG_MPU_EN_KR | REG_MPU_EN_KW | REG_MPU_EN_KE | REG_MPU_EN_UR | REG_MPU_EN_UW | REG_MPU_EN_UE

  ; full access for user ===> if a user can access, kernel can too
  .equ REG_MPU_EN_FULL_ACCESS, REG_MPU_EN_UR | REG_MPU_EN_UW | REG_MPU_EN_UE

  .equ MPU_SIZE_32B , 0b00100
  .equ MPU_SIZE_64B , 0b00101
  .equ MPU_SIZE_128B, 0b00110
  .equ MPU_SIZE_256B, 0b00111
  .equ MPU_SIZE_512B, 0b01000
  .equ MPU_SIZE_1K  , 0b01001
  .equ MPU_SIZE_2K  , 0b01010
  .equ MPU_SIZE_4K  , 0b01011
  .equ MPU_SIZE_8K  , 0b01100
  .equ MPU_SIZE_16K , 0b01101
  .equ MPU_SIZE_32K , 0b01110
  .equ MPU_SIZE_64K , 0b01111
  .equ MPU_SIZE_128K, 0b10000
  .equ MPU_SIZE_256K, 0b10001
  .equ MPU_SIZE_512K, 0b10010
  .equ MPU_SIZE_1M  , 0b10011
  .equ MPU_SIZE_2M  , 0b10100
  .equ MPU_SIZE_4M  , 0b10101
  .equ MPU_SIZE_8M  , 0b10110
  .equ MPU_SIZE_16M , 0b10111
  .equ MPU_SIZE_32M , 0b11000
  .equ MPU_SIZE_64M , 0b11001
  .equ MPU_SIZE_128M, 0b11010
  .equ MPU_SIZE_256M, 0b11011
  .equ MPU_SIZE_512M, 0b11100
  .equ MPU_SIZE_1G  , 0b11101
  .equ MPU_SIZE_2G  , 0b11110
  .equ MPU_SIZE_4G  , 0b11111

  ; least byte is used for region
  .equ MPU_ECR_FETCH,   0x060000
  .equ MPU_ECR_READ,    0x060100
  .equ MPU_ECR_WRITE,   0x060200
  .equ MPU_ECR_RW,      0x060300

  .equ PROTV_FETCH_MPU, 0x060004
  .equ PROTV_READ_MPU,  0x060104
  .equ PROTV_WRITE_MPU, 0x060204
  .equ PROTV_RW_MPU,    0x060304

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; Macro:     mpu_enable
; Regs used: r11
;
; Enable MPU on ARC HS systems
; "def_access" determines the _default region_ access
.macro mpu_enable   def_access=REG_MPU_EN_FULL_ACCESS
  mov   r11, \def_access
  or    r11, r11, REG_MPU_EN_EN
  and   r11, r11, REG_MPU_EN_MSK
  sr    r11, [mpuen]
.endm

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

.macro mpu_disable
  mov   r11, 0
  sr    r11, [mpuen]
.endm

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; Macro:     mpu_add_base
; Regs used: r11
;
; Adds the base address to the given MPU base register.
; "reg" is the mpu base register: mpurdb0 ... mpurdb15
; "addr" is the base address you are interested in, e.g.: 0x4000
.macro mpu_add_base   reg, addr
  mov   r11, \addr
  and   r11, r11, 0xffffffe0  ; the last 5 bits must be 0
  or    r11, r11, 1           ; set valid flag
  sr    r11, [\reg]
.endm

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; Macro:     mpu_add_region
; Regs used: r11, r12
;
; Adds the region permission and size to the given MPU permission register.
; "reg" is the mpu permission register: mpurdp0 ... mpurdp15
; "access" detemines the access type
; "size" is the region size: 00100b (32 bytes) ... 11111b (4 gigabytes)
.macro mpu_add_region   reg, access, size=0b100
  mov   r12, \size
  and   r11, r12, 3           ; get the lower 2 bits
  asl   r12, r12, 7           ; getting the upper 3 bits in position
  and   r12, r12, 0xe00       ; keeping only bits[11:9] in place
  or    r11, r11, r12         ; r11 has the size bits now
  or    r11, r11, \access
  sr    r11, [\reg]
.endm

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; Macro:     mpu_reset
; Regs used: r11
;
; Resets all the regions and disables MPU
.macro mpu_reset
  lr   r12, [mpu_build]
  and  r11, r12, 0xff
  breq r11, 0, @6666f   ; no mpu? then skip!
  mpu_disable
  lsr  r12, r12, 8
  brlt r12, 1, @6666f   ; no region at all? then skip!
  mov  r11, 0
  sr   r11, [mpurdb0]
  sr   r11, [mpurdp0]
  brlt r12, 2, @6666f   ; only 1 region? then skip!
  sr   r11, [mpurdb1]
  sr   r11, [mpurdp1]
  brlt r12, 4, @6666f   ; only 2 regions? then skip!
  sr   r11, [mpurdb2]
  sr   r11, [mpurdp2]
  sr   r11, [mpurdb3]
  sr   r11, [mpurdp3]
  brlt r12, 8, @6666f   ; only 4 regions? then skip!
  sr   r11, [mpurdb4]
  sr   r11, [mpurdp4]
  sr   r11, [mpurdb5]
  sr   r11, [mpurdp5]
  sr   r11, [mpurdb6]
  sr   r11, [mpurdp6]
  sr   r11, [mpurdb7]
  sr   r11, [mpurdp7]
  brlt r12, 16, @6666f  ; only 8 regions? then skip!
  sr   r11, [mpurdb8]
  sr   r11, [mpurdp8]
  sr   r11, [mpurdb9]
  sr   r11, [mpurdp9]
  sr   r11, [mpurdb10]
  sr   r11, [mpurdp10]
  sr   r11, [mpurdb11]
  sr   r11, [mpurdp11]
  sr   r11, [mpurdb12]
  sr   r11, [mpurdp12]
  sr   r11, [mpurdb13]
  sr   r11, [mpurdp13]
  sr   r11, [mpurdb14]
  sr   r11, [mpurdp14]
  sr   r11, [mpurdb15]
  sr   r11, [mpurdp15]
6666:
.endm

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; These are the parameters that the ProtV exception routine uses as reference
; There are tests that want to disable the R(ead) permission for the whole
; memory layout, but they do make an exception for the parameters below. To
; achieve that, they allow reading for a region of 32 bytes (minimum possible
; size for a region) that these parameters reside in. Therefore, we have to
; make sure these are the one and only things in this region by guarding them
; with ".align 32" and nothing else.
  .align 32
mpu_ecr_ref: .word 0x0
ecr_ref    : .word 0x0
efa_ref    : .word 0x0
eret_ref   : .word 0x0
cont_addr  : .word 0x0
  .align 32

; Exception: Protection Violation
; Regs used: r11, r12
;
; This is a parameterized ProtV exception that will check the followings:
; mpuic == mpu_ecr_ref
; ecr   == ecr_ref
; efa   == efa_ref
; eret  == eret_ref
; If everything passes, it will jump to 'cont_addr' parameter. It will clear
; the user bit before the jump, i.e. if an exception is raised in user mode,
; the continuation after exception will be in kernel mode. If the check
; should fail, it jumps to "fail" label which must exist in the test file.
; The parameters must be set beforehand using 'mpu_set_except_params' macro.
; Last but not least, this requires ivt.S file to be compiled and linked.
  .align 4
  .global instruction_error
  .global EV_PrivilegeV
  .global EV_ProtV
  .type instruction_error, @function
  .type EV_PrivilegeV, @function
  .type EV_ProtV, @function
instruction_error:
EV_PrivilegeV:
EV_ProtV:
  ld   r11, [mpu_ecr_ref]
  lr   r12, [mpuic]
  cmp  r12, r11
  bne  @fail
  ld   r11, [ecr_ref]
  lr   r12, [ecr]
  cmp  r12, r11
  bne  @fail
  ld   r11, [eret_ref]
  lr   r12, [eret]
  cmp  r12, r11
  bne  @fail
  ld   r11, [efa_ref]
  lr   r12, [efa]
  cmp  r12, r11
  bne  @fail
  ; going back to the given address in kernel mode
  ld   r11, [cont_addr]
  sr   r11, [eret]
  lr   r11, [erstatus]
  and  r11, r11, ~0x80       ; clear user mode bit
  sr   r11, [erstatus]
  rtie

; Macro:      mpu_set_except_params
; Regs used:  r11
;
; This macro writes the provided parameters to a temporary place holder
; that later will be used by ProtV exception above to verify as reference.
.macro mpu_set_except_params mpu_ecr, ecr, efa, eret, continue
  mov  r11, \mpu_ecr
  st   r11, [mpu_ecr_ref]
  mov  r11, \ecr
  st   r11, [ecr_ref]
  mov  r11, \efa
  st   r11, [efa_ref]
  mov  r11, \eret
  st   r11, [eret_ref]
  mov  r11, \continue
  st   r11, [cont_addr]
.endm

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; Macro:     mpu_verify_data
; Regs used: r11, r12
;
; Reads the data at the given address and check if it holds a certain value.
; It requires the test source file to have "fail" label.
.macro mpu_verify_data  ref, addr
  ld   r11, [\addr]
  mov  r12, \ref
  cmp  r11, r12
  bne  @fail
.endm

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; Macro:     mpu_write_data
; Regs used: r11, r12
.macro mpu_write_data  data, addr
  mov  r11, \data
  st   r11, [\addr]
.endm

; vim: set syntax=asm ts=2 sw=2 et:
