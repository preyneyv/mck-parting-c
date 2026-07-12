PHDRS
{
  image PT_LOAD FLAGS(5);
  writable PT_LOAD FLAGS(6);
}

SECTIONS
{
  . = 0;
  __prism_image_start = .;

  .text : ALIGN(4)
  {
    *(.text .text.*)
    *(.rodata .rodata.*)
    *(.glue_7t) *(.glue_7)
  } :image

  .prism_cartridge : ALIGN(4)
  {
    __prism_descriptor_start = .;
    KEEP(*(.prism_cartridge))
    __prism_descriptor_end = .;
  } :image

  .got : ALIGN(4)
  {
    __prism_got_start = .;
    *(.got .got.*)
    __prism_got_end = .;
  } :image

  __prism_image_end = ALIGN(4);
  __prism_rw_start = __prism_image_end;

  .data : ALIGN(4)
  {
    *(.data .data.*)
    __prism_rw_data_end = .;
  } :writable
  .bss (NOLOAD) : ALIGN(4)
  {
    *(.bss .bss.* COMMON)
    __prism_rw_end = .;
  } :writable

  __prism_rw_end = ALIGN(4);

  /DISCARD/ :
  {
    *(.comment)
    *(.ARM.attributes)
    *(.note*)
  }

  ASSERT((__prism_descriptor_end - __prism_descriptor_start) == 64,
         "a cartridge must export exactly one prism_cartridge_t")
}
