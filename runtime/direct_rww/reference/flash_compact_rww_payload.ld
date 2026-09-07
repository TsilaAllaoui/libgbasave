ENTRY(m36c_payload_start)
SECTIONS {
  . = 0;
  .text : { *(.text*) }
  m36c_payload_end = .;
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) }
}
