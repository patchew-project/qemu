ENTRY(_start)

PHDRS
{
  text PT_LOAD FILEHDR PHDRS;
  prop 0x6474e553 ;  /* PT_GNU_PROPERTY */
}

SECTIONS
{
  . = 1M + SIZEOF_HEADERS;
  .note.gnu.property : { *(.note.gnu.property) } :text :prop
  .text : { *(.text) *(.rodata) } :text
  /DISCARD/ : { *(.note.gnu.build-id) }
}
