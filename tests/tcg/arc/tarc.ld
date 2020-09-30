ENTRY(main)
SECTIONS
{
 .ivt 0x00 :
 {
  KEEP (*(.ivt));
 }
 . = 0x100;
 .text : { *(.text) }
 .data : { *(.data) }
 .bss : { *(.bss COMMON) }
 . = ALIGN(8);
 . = . + 0x1000; /* 4kB of stack memory */
 stack_top = .;
}
