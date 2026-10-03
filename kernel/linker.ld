/* linker.ld: ARM64 bare-metal linker script for QEMU -M virt
 *
 * QEMU virt memory map:
 *   0x40000000+  DRAM (128 MB default)
 *   0x09000000   PL011 UART
 *
 * Kernel layout:
 *   0x40000000   .text.boot  (_start)
 *   ...          .text / .rodata / .data / .bss
 *   0x40200000   stack (64 KB)
 *   0x40300000   heap  (1 MB)
 *
 * The kernel reads __bss_start/__bss_end and __heap_start/__heap_end as `extern`
 * globals; boot.s reads __stack_top.
 */

ENTRY(_start)

SECTIONS {
    . = 0x40000000;

    .text.boot : {
        *(.text.boot)
    }

    .text : {
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    .bss (NOLOAD) : ALIGN(16) {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    }

    . = 0x40200000;
    __stack_bottom = .;
    . += 0x10000;
    __stack_top = .;

    . = 0x40300000;
    __heap_start = .;
    . += 0x100000;
    __heap_end = .;
}
