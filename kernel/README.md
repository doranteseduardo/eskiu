# Eskiu Kernel v0.1

Bare-metal ARM64 kernel targeting QEMU `-M virt`. Boots without libc, sets up a stack, initialises a PL011 UART, and prints to the serial console.

This is the v0.1 milestone for the Eskiu language: Eskiu code running on bare metal.

## Architecture

```
boot.s       ARM64 entry point: sets up stack, calls kernel_main
uart.esk     PL011 UART driver (MMIO via inline asm)
alloc.esk    Bump allocator (esk_alloc / esk_free for --freestanding)
kernel.esk   kernel_main: hello world + allocator test
linker.ld    Memory layout (code at 0x40000000, stack at 0x40200000)
```

## Prerequisites

```bash
brew install lld qemu
```

Or use the Makefile target:

```bash
make setup
```

## Build and run

```bash
cd kernel
make        # compiles and links kernel.elf
make run    # launches QEMU
```

Expected output:

```
  ___     _    _
 | __|___| | _(_)_  _
 | _|(_-< || / / || |
 |___/__/_|\/_/ \_,_|

Eskiu v0.1 kernel
Running on QEMU -M virt (ARM64)
boot.s set up the stack; the rest is Eskiu.

UART base:  0x0000000009000000
Heap start: 0x0000000040300000
alloc(64):  0x0000000040300000 ok

Kernel halted.
```

Press `Ctrl-A X` to exit QEMU.

## Memory layout

| Region       | Address              | Size  |
|--------------|----------------------|-------|
| Kernel code  | `0x40000000`         | (n/a) |
| Stack        | `0x40200000`         | 64 KB |
| Heap         | `0x40300000`         | 1 MB  |
| UART DR      | `0x09000000`         | MMIO  |

## How it is compiled

```bash
# Each Eskiu file is cross-compiled to an ELF arm64 object
eskiuc kernel.esk --target aarch64-unknown-none-elf --freestanding -o kernel.o

# boot.s is assembled with Clang
clang --target=aarch64-unknown-none-elf -c boot.s -o boot.o

# Objects are linked into a bare-metal ELF with lld
ld.lld -T linker.ld -nostdlib -static -o kernel.elf boot.o kernel.o ...
```
