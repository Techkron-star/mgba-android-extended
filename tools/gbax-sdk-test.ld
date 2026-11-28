ENTRY(_start)
SECTIONS {
    . = 0x08000000;
    .text : { KEEP(*(.text.start)) *(.text*) *(.rodata*) }
    /DISCARD/ : { *(.comment*) *(.note*) *(.ARM.attributes*) *(.ARM.exidx*) *(.ARM.extab*) }
}
