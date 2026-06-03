# Cryptographic Image-Processing Pipeline

Eskiu port of a real-world cryptographic image-processing pipeline. Decrypts
multi-round AES-256-CBC + RSA-8192 encoded data extracted from QR codes in an
image and produces structured output.

## Overview

The pipeline performs three sequential stages:

1. **QR extraction** — load an image (HEIC/JPEG), detect and decode two QR
   codes using zxing-cpp via CoreGraphics, yielding two raw byte buffers.
2. **Crypto** — 3-round AES-256-CBC + RSA-8192 decryption of the QR payloads,
   producing plaintext.
3. **Structured output** — parse the plaintext (pipe-delimited fields + embedded
   WebP blob) into usable data.

Status: **COMPLETE and running.**

## Benchmark Results

Measured against a real credential image on Apple Silicon (macOS).

| Stage         | Eskiu    | Reference C |
|---------------|----------|-------------|
| QR extraction | 71.7 ms  | 185.5 ms    |
| Crypto        |  2.8 ms  |   2.9 ms    |
| Output decode |  <1 ms   |   0.5 ms    |
| **Total**     | **74.4 ms** | **188.9 ms** |

2.5x faster than reference C overall. The crypto pipeline matches hand-written
C within 0.1 ms.

## Files

| File | Description |
|------|-------------|
| `types.esk` | Core data structures (QRPair, NoSoKeys, IneResult, IneFields) |
| `extern.esk` | C library declarations — libc + no_so_crypto API |
| `stage1_qr.esk` | QR extraction wrapper (calls `ine_qr_extract` from C shim) |
| `stage2_crypto.esk` | Crypto pipeline — delegates AES/RSA work to C |
| `stage3_output.esk` | Output decode wrapper — delegates parsing to C |
| `main.esk` | Orchestration: QR extraction -> C crypto -> C decode -> output |
| `qr_extract.c` | C shim — `ine_qr_extract` wraps `qr_extract` from C library |
| `qr_extract_impl.cpp` | CoreGraphics + zxing-cpp 3.x image detection |
| `Makefile` | Build instructions |
| `README.md` | This file |

## Architecture

Eskiu owns the top-level orchestration; C owns the compute-heavy stages.

```
[Image file]
     |
     v
stage1_qr.esk  -->  qr_extract.c  -->  qr_extract_impl.cpp
     |                                 (CoreGraphics + zxing-cpp)
     v
stage2_crypto.esk  -->  no_so_crypto.o
     |                  (3-round AES-256-CBC + RSA-8192)
     v
stage3_output.esk  -->  output_decode.o
     |                  (pipe-delimited parse + WebP extraction)
     v
  structured output
```

**Eskiu handles:** timing, buffer management, file I/O, orchestration.

**C handles:** image loading, QR detection, AES/RSA crypto, output parsing.

## Build

### Prerequisites (macOS)

```bash
brew install llvm zxing-cpp openssl
```

### Dependencies

This project links against a companion C library that provides:

- `no_so_crypto.o` — AES-256-CBC + RSA-8192 decryption
- `output_decode.o` — structured output parsing
- `qr_extract.o` — QR detection entry point
- `base64.o` — base64 helpers

### Compile and link

```bash
# Compile the Eskiu source
eskiuc main.esk -o main.o

# Link
clang main.o no_so_crypto.o output_decode.o qr_extract.o base64.o \
  qr_extract_impl.cpp.o \
  -lcrypto -lZXing \
  -framework CoreFoundation -framework CoreGraphics -framework ImageIO \
  -o ine_decoder_eskiu
```

See the `Makefile` for the exact flags used in CI.

## Key Lessons

Issues encountered and fixed while building this pipeline:

- **Integer width coercion** — comparing a `uint8` field against a literal integer constant caused an ICmp width mismatch at runtime. Fixed by emitting a `ZExt` in the LLVM IR codegen for ICmp operands.
- **Mixed int/float arithmetic** — multiplying an `int64` value by a `double` constant required an explicit `SIToFP` promotion. The compiler now inserts this automatically when operand types differ.
- **extern function parameter naming** — using a reserved keyword (`in`) as a parameter name in an `extern` declaration broke the parser. Renamed to avoid the conflict.
- **import path resolution** — import paths in Eskiu are resolved relative to the importing file, not the compiler working directory. All imports in this project use paths relative to `main.esk`.

---

Written to `/Users/dorantes/Documents/Github/eskiu/ine_decoder/README.md`.
