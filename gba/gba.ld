/* GBA Studio — linker script
 *
 * Layout assumes the ROM header occupies exactly 0xC0 bytes at 0x08000000,
 * so the entry point must land at 0x080000C0.
 */
OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_start)

MEMORY
{
  rom   (rx)  : ORIGIN = 0x08000000, LENGTH = 32M
  ewram (rwx) : ORIGIN = 0x02000000, LENGTH = 256K
  iwram (rwx) : ORIGIN = 0x03000000, LENGTH = 28K
}

SECTIONS
{
  .rom 0x08000000 :
  {
    /* The header must be exactly 0xC0 bytes so .text.startup lands on
     * 0x080000C0. Neither ". = 0x080000C0" nor an ASSERT on "." works here:
     * inside an output section this linker reports "." as an offset from the
     * section start, not an absolute address. The invariant is therefore
     * checked after linking by tools/build-gba.ps1 (nm _start) and by
     * tools/romcheck.ps1 (decode the branch at offset 0). */
    KEEP(*(.rom_header))
    __text_start = .;
    *(.text.startup)
    *(.text*)
    *(.rodata*)
    . = ALIGN(4);
    __rom_payload_end = .;
  } > rom

  .iwram 0x03000000 : AT(ALIGN(LOADADDR(.rom) + SIZEOF(.rom), 4))
  {
    __iwram_start = .;
    *(.iwram.text)
    *(.iwram.data)
    . = ALIGN(4);
    __iwram_end = .;
  } > iwram
  __iwram_lma  = LOADADDR(.iwram);
  __iwram_size = __iwram_end - __iwram_start;

  .data 0x02000000 : AT(ALIGN(LOADADDR(.iwram) + SIZEOF(.iwram), 4))
  {
    __data_start = .;
    *(.data*)
    . = ALIGN(4);
    __data_end = .;
  } > ewram
  __data_lma  = LOADADDR(.data);
  __data_size = __data_end - __data_start;

  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    __bss_start = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    __bss_end = .;
  } > ewram
  __bss_size = __bss_end - __bss_start;

  /DISCARD/ : { *(.comment) *(.note*) *(.ARM.attributes) *(.eh_frame*) }
}
/* The entry point is guarded two ways: the ASSERT inside .rom above, and
 * tools/romcheck.ps1, which decodes the ARM branch at offset 0 in the final
 * image and requires it to target 0x080000C0. */
