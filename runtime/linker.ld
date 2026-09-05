ENTRY(gbashIdentifyFlash)

SECTIONS
{
    . = 0x08400000;
    __runtime_start = .;

    .runtime_config ALIGN(4) :
    {
        KEEP(*(.runtime_config))
    }

    .text ALIGN(4) :
    {
        *(.text.runtime)
        KEEP(*(.text.runtime.arm))
        *(.text.runtime.*)
        *(.text*)
        *(.rodata*)
    }

    .sram_stack_worker ALIGN(4) :
    {
        __sram_stack_worker_start = .;
        KEEP(*(.sram_stack_worker))
        KEEP(*(.sram_stack_worker.*))
        __sram_stack_worker_end = .;
    }

    .ram_worker ALIGN(4) :
    {
        __ram_worker_start = .;
        KEEP(*(.ram_worker))
        KEEP(*(.ram_worker.*))
        __ram_worker_end = .;
    }

    __runtime_end = .;

    /DISCARD/ :
    {
        *(.ARM.exidx*)
        *(.ARM.extab*)
        *(.comment*)
        *(.note*)
        *(.eh_frame*)
    }
}
