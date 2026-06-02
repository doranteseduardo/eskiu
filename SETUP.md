# Eskiu Lang — Project Setup Instructions

This document walks through the Phase 0 setup to get the compiler building and running.

## Prerequisites

The Eskiu compiler requires:
- **LLVM 17+** (C++ backend)
- **CMake 3.20+** (build system)
- **Clang 14+** or **GCC 11+** (C++ compiler)

### macOS Setup

```bash
# Install LLVM 17
brew install llvm@17

# Install CMake (if not present)
brew install cmake

# Verify installations
llvm-config-17 --version
cmake --version
```

After installation, add LLVM to your PATH:
```bash
export PATH="/usr/local/opt/llvm@17/bin:$PATH"
export LLVM_CONFIG=/usr/local/opt/llvm@17/bin/llvm-config
```

(Or add these lines to your `~/.zshrc` or `~/.bash_profile`.)

### Linux Setup (Ubuntu/Debian)

```bash
# Install LLVM 17 and build tools
sudo apt-get update
sudo apt-get install -y cmake llvm-17 llvm-17-dev clang-17

# Verify
llvm-config-17 --version
cmake --version
```

## Building the Compiler

```bash
cd ~/Documents/Github/eskiu
mkdir -p build
cd build
cmake .. -DLLVM_DIR=/usr/local/opt/llvm@17/lib/cmake/llvm  # macOS
cmake ..                                                     # Linux (auto-detects)
cmake --build .
```

## Testing the Build

```bash
# Test version output
./build/eskiuc --version
# Expected: Eskiu 0.0.1 (LLVM 17.x)

# Test with a source file (not yet implemented, will error)
./build/eskiuc hello.esk -o hello
```

## Project Structure

```
eskiu/
├── CMakeLists.txt         ← Build configuration
├── main.cpp               ← Compiler entry point
├── lexer/
│   ├── lexer.h            ← Token definitions and Lexer class
│   └── lexer.cpp          ← Tokenizer implementation
├── parser/                ← (Phase 2)
├── ast/                   ← (Phase 2)
├── sema/                  ← (Phase 4)
├── codegen/               ← (Phase 3)
├── runtime/               ← (Phases 6+)
└── stdlib/                ← (Phase 7+)
```

## Next Steps (Phase 1)

Once the build succeeds:

1. **Test the lexer** with sample Eskiu code:
   ```bash
   ./build/eskiuc --test-lexer path/to/file.esk
   ```

2. **Expand lexer** coverage for all token types

3. **Add AST** structures for parsing

## Troubleshooting

**CMake can't find LLVM:**
```bash
# macOS: explicitly provide LLVM path
cmake .. -DLLVM_DIR=/usr/local/opt/llvm@17/lib/cmake/llvm

# Linux: verify LLVM 17 is installed
dpkg -l | grep llvm-17
```

**Build fails with linking errors:**
- Ensure `llvm-config-17 --version` works from your terminal
- Verify CMake detected the correct LLVM components in the build output

**Header not found `llvm/...`:**
- Check that LLVM_INCLUDE_DIRS is set in CMakeLists.txt
- Run: `llvm-config-17 --includedir`
