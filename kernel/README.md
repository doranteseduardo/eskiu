# Eskiu Kernel

Bare-metal ARM64 kernel targeting QEMU `-M virt`. It boots without libc, prints to
the PL011 serial console, reads a few system registers, runs a heap, and switches the
machine off when it is done.

The kernel was the v0.1 milestone for the language: Eskiu code running on bare metal.
It has since been updated to current Eskiu.

## What it shows

- `volatile` MMIO: the UART driver reads the flag register through a bitfield struct
  (`UartFlags`) and waits for TXFF to clear before each write to the data register.
- `extern` globals: the linker-script symbols `__bss_start`/`__bss_end` and
  `__heap_start`/`__heap_end` are declared as `extern uint8` and used by address.
- `--freestanding`: `alloc<T>`/`free` from `<mem>` call `esk_alloc`/`esk_free`, which
  wrap the stdlib `Bump` allocator from `<alloc>` over the linker-defined heap.
- Inline-asm output operands: `mrs` reads `CurrentEL`, `CNTFRQ_EL0` and `CNTVCT_EL0`.
- A sum type with `match` (`Event`), `defer`, and `const` addresses.
- PSCI `SYSTEM_OFF` through `hvc #0`, so QEMU exits by itself at the end.

## Files

```
boot.s       ARM64 entry point: sets up the stack, calls kernel_main
kernel.esk   kernel_main: clears .bss, reports boot events, tests the heap
uart.esk     PL011 UART driver (volatile MMIO)
cpu.esk      system registers and PSCI power off (inline asm)
alloc.esk    esk_alloc / esk_free over a stdlib Bump allocator
linker.ld    memory layout and the symbols the kernel reads
check.sh     boots the kernel headless and checks its output (make check)
```

`.bss` is cleared by `zero_bss()` at the top of `kernel_main` rather than in `boot.s`.
Nothing before that point reads a global, so the loop can live in Eskiu, and the
assembly stub stays limited to what Eskiu cannot do (setting `sp`).

## Prerequisites

A built `eskiuc` (the Makefile uses `../build/eskiuc`), clang (to assemble `boot.s`),
`ld.lld`, and QEMU. On macOS:

```bash
brew install lld qemu     # or: make setup
```

On Debian or Ubuntu: `apt-get install lld qemu-system-arm`.

Every tool is a Makefile variable you can override:

```bash
make check ESKIUC=../build22/eskiuc CLANG=clang-22 LLD=ld.lld-22 QEMU=qemu-system-aarch64
```

## Build and run

```bash
cd kernel
make          # compiles and links kernel.elf
make run      # boots it in QEMU on this terminal
make check    # boots it headless and checks the UART output
```

`make run` prints the output below and QEMU exits when the kernel powers off. Press
`Ctrl-A X` to leave QEMU earlier.

```
  ___     _    _
 | __|___| | _(_)_  _
 | _|(_-< || / / || |
 |___/__/_|\_/_/ \_,_|

Eskiu bare-metal kernel
Running on QEMU -M virt (ARM64)
boot.s set up the stack; the rest is Eskiu.

Running at: EL1
Timer:      62500000 Hz
UART base:  0x0000000009000000
Heap:       0x0000000040300000 - 0x0000000040400000

alloc(64): 0x0000000040300000
alloc(4096): 0x0000000040300040
alloc(4194304): out of memory
Heap used:  4160 bytes

Uptime:     1964 us
Powering off.
```

`make check` runs the same boot with `-nographic` and stdin closed, kills QEMU after
`TIMEOUT` seconds (default 30) in case the kernel hangs, and fails unless QEMU exited
cleanly and every expected line (listed in `check.sh`) is present. The timer
frequency and the uptime vary, so those lines only have to be numbers. CI runs it on
Linux.

## Memory layout

| Region       | Address              | Size  |
|--------------|----------------------|-------|
| Kernel code  | `0x40000000`         | (n/a) |
| Stack        | `0x40200000`         | 64 KB |
| Heap         | `0x40300000`         | 1 MB  |
| UART (PL011) | `0x09000000`         | MMIO  |

## How it is compiled

```bash
# kernel.esk imports uart.esk, cpu.esk and alloc.esk; all compile into one ELF arm64 object
eskiuc kernel.esk --target aarch64-unknown-none-elf --freestanding -o kernel.o

# boot.s is assembled with Clang
clang --target=aarch64-unknown-none-elf -c boot.s -o boot.o

# Objects are linked into a bare-metal ELF with lld
ld.lld -T linker.ld -nostdlib -static -o kernel.elf boot.o kernel.o
```

The kernel is built at the default `-O0`. At `-O2` LLVM turns the byte-clearing loops
into `memset` calls, which this libc-free link does not provide.
