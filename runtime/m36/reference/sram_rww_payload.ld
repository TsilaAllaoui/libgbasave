ENTRY(payload_entry)
SECTIONS {
 . = 0;
 .text : { *(.text*) *(.rodata*) }
 /DISCARD/ : { *(.comment*) *(.ARM.attributes*) *(.note*) }
}
