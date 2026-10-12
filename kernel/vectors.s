// vectors.s: the EL1 exception vector table and the trap entry/exit path
//
// Every exception saves the interrupted state as a TrapFrame on the current stack
// (x0-x30, ELR_EL1, SPSR_EL1: 272 bytes, the layout of `TrapFrame` in trap.esk) and
// calls an Eskiu handler with a pointer to it. The handler returns the frame to
// resume, and trap_return loads sp from it before restoring the registers. That is
// also the task switch: the scheduler returns another task's frame, which sits on
// that task's stack.
//
// The kernel is built with --mattr=-fp-armv8,-neon, so the FP/SIMD registers are
// never used and need no saving.

.equ FRAME_SIZE, 272

.macro SAVE_FRAME
    sub  sp, sp, #FRAME_SIZE
    stp  x0,  x1,  [sp, #0]
    stp  x2,  x3,  [sp, #16]
    stp  x4,  x5,  [sp, #32]
    stp  x6,  x7,  [sp, #48]
    stp  x8,  x9,  [sp, #64]
    stp  x10, x11, [sp, #80]
    stp  x12, x13, [sp, #96]
    stp  x14, x15, [sp, #112]
    stp  x16, x17, [sp, #128]
    stp  x18, x19, [sp, #144]
    stp  x20, x21, [sp, #160]
    stp  x22, x23, [sp, #176]
    stp  x24, x25, [sp, #192]
    stp  x26, x27, [sp, #208]
    stp  x28, x29, [sp, #224]
    mrs  x9,  elr_el1
    mrs  x10, spsr_el1
    stp  x30, x9,  [sp, #240]
    str  x10, [sp, #256]
.endm

// Each slot is 0x80 bytes; it only branches to the shared code below.
.macro SLOT target
    .balign 0x80
    b    \target
.endm

.section .text
.balign 0x800
.global exception_vectors
exception_vectors:
    SLOT unexpected_0       // current EL, SP_EL0: sync
    SLOT unexpected_1       //                     IRQ
    SLOT unexpected_2       //                     FIQ
    SLOT unexpected_3       //                     SError
    SLOT el1_sync           // current EL, SP_EL1: sync (the kernel runs here)
    SLOT el1_irq            //                     IRQ
    SLOT unexpected_6       //                     FIQ
    SLOT unexpected_7       //                     SError
    SLOT unexpected_8       // lower EL, AArch64
    SLOT unexpected_9
    SLOT unexpected_10
    SLOT unexpected_11
    SLOT unexpected_12      // lower EL, AArch32
    SLOT unexpected_13
    SLOT unexpected_14
    SLOT unexpected_15

el1_sync:
    SAVE_FRAME
    mov  x0, sp
    bl   trap_sync
    b    trap_return

el1_irq:
    SAVE_FRAME
    mov  x0, sp
    bl   trap_irq
    b    trap_return

// trap_unexpected(frame, slot) reports the exception and does not return.
.macro UNEXPECTED n
unexpected_\n:
    SAVE_FRAME
    mov  x0, sp
    mov  x1, #\n
    bl   trap_unexpected
    b    .
.endm

UNEXPECTED 0
UNEXPECTED 1
UNEXPECTED 2
UNEXPECTED 3
UNEXPECTED 6
UNEXPECTED 7
UNEXPECTED 8
UNEXPECTED 9
UNEXPECTED 10
UNEXPECTED 11
UNEXPECTED 12
UNEXPECTED 13
UNEXPECTED 14
UNEXPECTED 15

// x0: the frame to resume. Also the first entry into a new task.
.global trap_return
trap_return:
    mov  sp, x0
    ldp  x30, x9,  [sp, #240]
    ldr  x10, [sp, #256]
    msr  elr_el1, x9
    msr  spsr_el1, x10
    ldp  x0,  x1,  [sp, #0]
    ldp  x2,  x3,  [sp, #16]
    ldp  x4,  x5,  [sp, #32]
    ldp  x6,  x7,  [sp, #48]
    ldp  x8,  x9,  [sp, #64]
    ldp  x10, x11, [sp, #80]
    ldp  x12, x13, [sp, #96]
    ldp  x14, x15, [sp, #112]
    ldp  x16, x17, [sp, #128]
    ldp  x18, x19, [sp, #144]
    ldp  x20, x21, [sp, #160]
    ldp  x22, x23, [sp, #176]
    ldp  x24, x25, [sp, #192]
    ldp  x26, x27, [sp, #208]
    ldp  x28, x29, [sp, #224]
    add  sp, sp, #FRAME_SIZE
    eret
