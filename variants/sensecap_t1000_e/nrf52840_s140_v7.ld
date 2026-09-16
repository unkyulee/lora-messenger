/* S140 7.3.0 occupies 0x1000 (MBR) + 0x26000 (SoftDevice).
 * Reserve 0xED000..0xF3FFF for Adafruit InternalFS; bootloader above it.
 * Section definitions come from the installed Adafruit BSP. */
SEARCH_DIR(.)
GROUP(-lgcc -lc -lnosys)
MEMORY
{
  FLASH (rx) : ORIGIN = 0x27000, LENGTH = 0xED000 - 0x27000
  RAM (rwx) : ORIGIN = 0x20006000, LENGTH = 0x20040000 - 0x20006000
}
SECTIONS
{
  .svc_data : {
    . = ALIGN(4);
    PROVIDE(__start_svc_data = .);
    KEEP(*(.svc_data))
    PROVIDE(__stop_svc_data = .);
  } > RAM
  .fs_data : {
    . = ALIGN(4);
    PROVIDE(__start_fs_data = .);
    KEEP(*(.fs_data))
    PROVIDE(__stop_fs_data = .);
  } > RAM
} INSERT AFTER .data;
INCLUDE "nrf52_common.ld"
