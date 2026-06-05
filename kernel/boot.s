// boot.s — ARM64 bare-metal entry point for QEMU -M virt
// Sets up the stack and jumps to kernel_main.
// Everything else is written in Eskiu.

.global _start
.section .text.boot, "ax"
_start:
    ldr x0, =__stack_top
    mov sp, x0
    bl  kernel_main
.hang:
    b   .hang
