# Cross-Compiling

`eskiuc` emits objects for a target other than the build host. Set the triple with
`--target`; three companion flags tune the machine and ABI:

| Flag        | Purpose                                                                          |
| ----------- | -------------------------------------------------------------------------------- |
| `--target`  | Target triple (e.g. `x86_64-linux-gnu`, `armv6k-none-eabihf`). Empty = host.     |
| `--mcpu`    | Target CPU passed to LLVM (e.g. `mpcore`). Empty = `generic` when cross, host CPU when native. |
| `--mattr`   | LLVM feature string, `-mattr` syntax (e.g. `+vfp2`).                              |
| `--reloc`   | Relocation model: `pic` (default), `static`, or `dynamic-no-pic`.                |

The registered backends are AArch64, x86-64, and 32-bit ARM. `eskiuc` only produces the
object file; link it with a cross toolchain that targets the same platform.

## Platform macros

The preprocessor predefines a platform macro so `import`ed stdlib code can branch per OS:

- An explicit `--target` containing `linux` defines `__linux__`; one containing `apple`,
  `darwin`, or `macos` defines `__APPLE__`; one containing `windows`, `win32`, or `mingw`
  defines `_WIN32` (plus `_WIN64` for a 64-bit arch).
- A **non-hosted** triple (OS `none`, such as `armv6k-none-eabihf`) defines neither. Bare
  metal has no host OS, so portable code guards that path explicitly.
- With no `--target`, the macro follows the build host.

`--freestanding` additionally predefines `__ESKIU_FREESTANDING__` and routes `alloc`/`free`
to caller-supplied `esk_alloc`/`esk_free` instead of libc.

## Hard-float ARM

A triple ending in `hf` (e.g. `armv6k-none-eabihf`) selects the hard-float ABI. LLVM then
stamps the `Tag_ABI_VFP_args` build attribute on the object, which a hard-float linker
requires to mix the object with hard-float libraries. Float arguments pass in VFP
registers (`s0`, `s1`, ...) rather than core registers.

Verify it on an emitted object:

```bash
llvm-readobj -A out.o | grep -A1 'VFP_args'
#   TagName: ABI_VFP_args
#   Description: AAPCS VFP
```

## Example: Nintendo 3DS

The 3DS is an ARM11 MPCore (`armv6k`) with VFPv2, hard-float, and a homebrew loader that
applies static relocations. Compile an Eskiu source to a 3DS object:

```bash
eskiuc lib.esk -o lib.o \
    --target armv6k-none-eabihf --mcpu mpcore --mattr +vfp2 \
    --reloc static --freestanding
```

Then link it with the [devkitARM](https://devkitpro.org/) toolchain, alongside a C entry
point and libctru, and package the result:

```bash
# C side (entry point + libctru calls), same arch flags:
arm-none-eabi-gcc -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft \
    -I$DEVKITPRO/libctru/include -c main.c -o main.o

# Link the C object + the Eskiu object into a 3DS ELF, then wrap it as .3dsx:
arm-none-eabi-gcc -specs=3dsx.specs -march=armv6k -mtune=mpcore -mfloat-abi=hard \
    main.o lib.o -L$DEVKITPRO/libctru/lib -lctru -lm -o app.elf
3dsxtool app.elf app.3dsx
```

The link succeeds only when both objects agree on the float ABI, which is why the `hf`
triple (and the resulting `Tag_ABI_VFP_args`) matters: without it the linker rejects the
Eskiu object against hard-float libctru.

Eskiu declarations compile to C-ABI symbols, so a `main.c` can call an Eskiu function by
declaring it `extern`. The self-hosted compiler does not yet forward `--mcpu`/`--mattr`/
`--reloc`, so 3DS builds use the reference `eskiuc`.

## Example: Windows (x86-64)

The x86 backend emits COFF objects for a Windows triple with no extra flags; LLVM lowers
the Microsoft x64 calling convention (first integer arguments in `rcx`/`rdx`/`r8`/`r9`)
from the triple:

```bash
eskiuc app.esk -o app.obj --target x86_64-pc-windows-gnu --freestanding
file app.obj    # Intel amd64 COFF object file
```

Link with a Windows linker. LLVM's `lld-link` needs an import library for any DLL the
program calls; generate one from a `.def` with `llvm-dlltool` (both ship with LLVM), so no
Windows SDK is required to link:

```bash
printf 'LIBRARY kernel32.dll\nEXPORTS\nExitProcess\nGetStdHandle\nWriteFile\n' > kernel32.def
llvm-dlltool -m i386:x86-64 -d kernel32.def -l kernel32.lib
lld-link /entry:mainCRTStartup /subsystem:console app.obj kernel32.lib /out:app.exe
```

A freestanding program provides its own entry point (`/entry:`) and calls Win32 directly.
A hosted program instead links the MinGW or MSVC C runtime and uses the ordinary `main`.
The resulting `.exe` runs on Windows, or under `wine` for local testing.

Exceptions (`try`/`throw`/`catch`) work on a mingw target: the compiler emits the SEH EH
personality (`__gxx_personality_seh0`) there instead of the Itanium `__gxx_personality_v0`,
so linking with a mingw `g++` (which supplies the `__cxa_*` runtime and the SEH unwinder)
produces a working `.exe`. This is exercised end to end on a native Windows runner in
`.github/workflows/windows.yml`.

The OS-level stdlib modules carry Windows backends: `<sysheap>` maps pages with
`VirtualAlloc`/`VirtualFree` (no `mmap`), `<time>` uses the Win32 clocks (`GetTickCount64`,
`GetSystemTimeAsFileTime`, `Sleep`), and `<threading>` links against winpthreads. Link a
program that uses them with a mingw `g++ ... -lpthread`. The async/networking stack
(`<eventloop>`/`<net>`, built on epoll/kqueue) is not yet ported to Windows IOCP.
