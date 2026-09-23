OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

PHDRS
{
    text    PT_LOAD FLAGS(5); /* Read + Execute */
    rodata  PT_LOAD FLAGS(4); /* Read Only */
    data    PT_LOAD FLAGS(6); /* Read + Write */
}

SECTIONS
{
    . = 0xffffffff80000000;

    .text : {
        *(.text .text.*)
    } :text

    /* Выравнивание до следующей 4K страницы перед сменой PHDR */
    . = ALIGN(0x1000);

    .rodata : {
        *(.rodata .rodata.*)
    } :rodata

    /* Выравнивание перед секциями с правом записи */
    . = ALIGN(0x1000);

    .requests : {
        KEEP(*(.requests))
        KEEP(*(.limine_requests))
    } :data

    .data : {
        *(.data .data.*)
    } :data

    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    } :data

    . = ALIGN(0x1000);

    /DISCARD/ : {
        *(.eh_frame)
        *(.note .note.*)
    }
}