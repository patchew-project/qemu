.include "macros.inc"

  start

  ; memset params
  mov    r0, data     ; address to write
  mov    r1, 0        ; data to write
  mov    r2, 0x13     ; size of memory to clear

  ; align the address
  and    r4, r0, 3
  rsub.f  lp_count, r4, 4
  lpne   @main_clear
  stb.ab r1, [r0, 1]
  sub    r2, r2, 1

main_clear:
  ; main setting to zero
  and.f    lp_count, r2, 0x1f
  lpne   @verify
  stb.ab r1, [r0, 1]

verify:
  ld     r1, [data, 0x12]
  cmp    r1, 0x66665500
  beq    @good
  print  "[FAIL] "
  j      @the_end
good:
  print  "[PASS] "
the_end:
  print  "LP04\n"
  end

.align 4
make_unaligned:
  .2byte 0xffff
data:
  .4byte 0x11111111
  .4byte 0x22222222
  .4byte 0x33333333
  .4byte 0x44444444
  .4byte 0x55555555
  .4byte 0x66666666
  .4byte 0x77777777
  .4byte 0x88888888
  .4byte 0x99999999
  .4byte 0xAAAAAAAA
