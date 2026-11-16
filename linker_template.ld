ENTRY(kernel_main)

SECTIONS
{
    . = TEXT_ADDR;
    .text : ALIGN(4K)
    {
        *(.text)
    } :text

    .rodata : ALIGN(4K)
    {
        *(.rodata)
    } :text

    .data : ALIGN(4K)
    {
        *(.data)
    } :data

    .bss :
    {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    }

    __kernel_end = .;
}
