# Cryptographic Image-Processing Pipeline

Eskiu port of a real-world cryptographic image-processing pipeline. Decrypts
multi-round AES-256-CBC + RSA-8192 encoded data extracted from QR codes in an
image and produces structured output.

**No dependency on any external C project.** The only C code is a 12-line shim
for calling a C++ QR detection library. All pipeline logic is in Eskiu.

## Overview

Three sequential stages:

1. **QR extraction**: load an image (HEIC/JPEG/PNG) via CoreGraphics, detect
   two QR codes using zxing-cpp, yield two raw 858-byte buffers.
2. **Crypto**: 3-round AES-256-CBC + RSA-8192 decryption; all cryptographic
   constants extracted from the original binary (no key file needed). Implemented
   in Eskiu calling OpenSSL via `extern`.
3. **Structured output**: parse pipe-delimited biographical text + WebP image
   blob, generate JSON. Implemented entirely in Eskiu.

Status: **COMPLETE.** Runs at 74.4 ms on Apple Silicon.

## Benchmark

Measured on Apple Silicon (macOS arm64) against a real credential image.

| Stage         | Eskiu     | Reference C |
|---------------|-----------|-------------|
| QR extraction | 71.7 ms   | 185.5 ms    |
| Crypto        |  2.8 ms   |   2.9 ms    |
| Output decode |  < 1 ms   |   0.5 ms    |
| **Total**     | **74.4 ms** | **188.9 ms** |

2.5× faster overall. Crypto pipeline within 0.1 ms of hand-written C.

## Files

| File | Language | Description |
|---|---|---|
| `types.esk` | Eskiu | `QRPair`, `BNPair`, `DecodePayload`, `DecodeOutput`, `PipelineTiming` |
| `extern.esk` | Eskiu | C function declarations (libc, OpenSSL EVP + BN, QR shim) |
| `crypto.esk` | **Eskiu** | AES + RSA pipeline: hex decode, base64, PKCS#1, 6-bit decode, `run_no_so_pipeline` |
| `output.esk` | **Eskiu** | Character table, field splitter, JSON builder, `decode_to_buffers` |
| `pipeline.esk` | Eskiu | `stage_extract`, `stage_crypto`, `stage_decode`: each returns `Result<int,string>` |
| `main.esk` | Eskiu | Entry point: orchestration, timing, file I/O |
| `qr_extract.c` | C | 12-line shim: `ine_qr_extract(path, *QRPair)` → `ine_qr_extract_impl()` |
| `qr_extract_impl.cpp` | C++ | CoreGraphics image loading + zxing-cpp 3.x QR detection |
| `Makefile` | (n/a) | Build instructions |

## Architecture

```
[Image file]
     │
     ▼
qr_extract_impl.cpp   ← C++: CoreGraphics + zxing-cpp (QR detection)
     │  858 bytes × 2
     ▼
crypto.esk            ← Eskiu: hex decode, AES-CBC, RSA, 6-bit decode
     │  1385 bytes plaintext
     ▼
output.esk            ← Eskiu: character table decode, JSON build
     │
     ▼
JSON + WebP
```

**External dependencies (system libraries only):**
- OpenSSL `libcrypto`: AES-256-CBC and BigNum for RSA
- zxing-cpp: QR code detection
- CoreFoundation / CoreGraphics / ImageIO: HEIC/JPEG image loading (macOS)

## Build

```bash
# Prerequisites (macOS)
brew install llvm zxing-cpp openssl

# Build the Eskiu compiler
cd /path/to/eskiu
cmake -B build && cmake --build build -j4

# Build the decoder
cd ine_decoder
make

# Run
./qr_decoder
```

Set `IMAGE_PATH` in `main.esk` to point at your image.
Output: `/tmp/qr_decoded.json` and `/tmp/qr_decoded.webp`.

## Compiler features exercised by this decoder

Writing this decoder in Eskiu required and validated several language features:

| Feature used | Where |
|---|---|
| Global string constants (`string CT_R1_KEY = "..."`) | `crypto.esk` |
| Multi-line string literals (`"abc"\n"def"`) | `crypto.esk` constants |
| Pointer subtraction `ptr - ptr → int64` | XML tag search in `crypto.esk` |
| `string[i]` → `char` indexing | Character lookups in `output.esk` |
| `**void` for OpenSSL BIGNUM out-params | `parse_pubkey_xml`, `run_round` |
| `BNPair` struct wrapping multi-value RSA key output | `run_round` |
| `Result<int,string>` error propagation | `pipeline.esk` stages |
| `int64` arithmetic with `uint8` arrays | Throughout |

## Compiler fixes found during development

| Bug | Symptom | Fix |
|---|---|---|
| `ptr - ptr` not implemented | Couldn't compute XML tag offsets | Added ptrtoint-subtract in codegen |
| `i8 - i32` codegen crash | `hex_digit()` arithmetic | ZExt widening for arithmetic ops |
| `string[i]` wrong type | Would compute type `"strin"` | Special-cased `string` in IndexExpr |
| `arr[i] = 0` stores wrong width | `starts[0] = 0` stored i32 into i64 slot | Store coercion via GEP element type |
| `uint8 X = 0x52` global mismatch | Global initializer type mismatch | ConstantInt cast in global VarDecl |
| `**char` "undefined struct '*char'" | Type checker rejected double-pointer | Strip all `*` levels in validateStructType |
| `"abc"\n"def"` not concatenated | Multi-line constants required single line | Adjacent string literal concatenation in parser |
