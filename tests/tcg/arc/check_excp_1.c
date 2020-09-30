#include <stdio.h>

void main (void)
{
  __builtin_arc_trap_s (0);
  printf ("[PASS] TRAPC:1\n");
  __builtin_arc_trap_s (1);
  printf ("[PASS] TRAPC:2\n");
}

void __attribute__ ((interrupt("ilink")))
EV_Trap (void)
{
  printf ("[PASS] TRAPC:IRQ\n");
}
