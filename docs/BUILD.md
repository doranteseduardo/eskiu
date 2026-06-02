# Build Guide

Complete instructions for building Eskiu from source on different platforms.

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [macOS Build](#macos-build)
3. [Linux Build](#linux-build)
4. [Build Configuration](#build-configuration)
5. [Troubleshooting](#troubleshooting)
6. [Development Build vs Release](#development-build-vs-release)

---

## Prerequisites

All platforms require:

- **CMake** 3.10 or later
- **LLVM** 14 or later (headers + libraries)
- **C++ compiler** supporting C++17 (GCC 7+, Clang 5+, or Apple Clang)

---

## macOS Build

### Step 1: Install Dependencies

Using Homebrew:

```bash
# Install LLVM (required for compilation)
brew install llvm cmake

# Verify installation
llvm-config --version
cmake --version
```

If you already have LLVM from a previous install:

```bash
brew upgrade llvm cmake
```

### Step 2: Configure LLVM Path

Before building Eskiu, tell CMake where LLVM is:

```bash
export LLVM_CONFIG=$(brew --prefix llvm)/bin/llvm-config
```

Add this to your shell profile (`~/.zshrc` or `~/.bash_profile`) to make it permanent:

```bash
export LLVM_CONFIG=$(brew --prefix llvm)/bin/llvm-config
```

### Step 3: Build Eskiu

```bash
cd ~/Documents/Github/eskiu
mkdir -p build
cd build
cmake ..
make -j$(sysctl -n hw.ncpu)
```

Verify the build:

```bash
./eskiu --version
# Expected: Eskiu 0.0.1 (LLVM 22.x.x)
```

---

## Linux Build

### Ubuntu/Debian

#### Step 1: Install Dependencies

```bash
sudo apt-get update
sudo apt-get install -y \
  cmake \
  llvm-14-dev \
  clang-14 \
  build-essential
```

#### Step 2: Build Eskiu

```bash
cd ~/eskiu  # or wherever you cloned it
mkdir -p build
cd build
cmake ..
make -j$(nproc)
```

#### Step 3: Verify

```bash
./eskiu --version
```

### Fedora/RHEL

```bash
sudo dnf install -y \
  cmake \
  llvm-devel \
  clang \
  gcc-c++ \
  make

cd ~/eskiu
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

### Alpine Linux (Minimal Footprint)

```bash
apk add --no-cache \
  cmake \
  llvm14-dev \
  clang \
  build-base

cd ~/eskiu
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

---

## Build Configuration

### CMake Options

When running `cmake ..`, you can customize the build with flags:

```bash
# Build type (Debug has symbols; Release is optimized)
cmake .. -DCMAKE_BUILD_TYPE=Release

# Specify LLVM location manually (if CMake can't find it)
cmake .. -DLLVM_CONFIG=/path/to/llvm-config

# Verbose build output (shows all compile commands)
cmake .. -DCMAKE_VERBOSE_MAKEFILE=ON

# Install to custom location
cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local/eskiu
```

### Example: Custom LLVM on Linux

If you have LLVM installed in a non-standard location:

```bash
LLVM_CONFIG=/opt/llvm-14/bin/llvm-config cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

---

## Development Build vs Release

### Development Build (Fast Iteration)

Debug symbols, no optimizations:

```bash
cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
```

Output binary: `./eskiu` (includes debug symbols, slower execution)

**Use when:** Writing compiler features, troubleshooting bugs

### Release Build (Production)

Optimized, small binary:

```bash
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

Output binary: `./eskiu` (optimized, smaller)

**Use when:** Benchmarking, final deployment

---

## Troubleshooting

### CMake Can't Find LLVM

**Error:**
```
CMake Error at CMakeLists.txt:XX (llvm_map_components_to_libnames):
  Unknown LLVM component specified
```

**Fix:** Explicitly set LLVM path:

```bash
cd build
cmake .. -DLLVM_CONFIG=/path/to/llvm-config
```

Find `llvm-config`:

```bash
# macOS
brew --prefix llvm  # then add /bin/llvm-config

# Linux
which llvm-config-14

# Or search
find /usr -name llvm-config 2>/dev/null
```

### LLVM Version Mismatch

**Error:**
```
error: LLVM version mismatch
```

**Fix:** Ensure you're using a compatible LLVM version (14+):

```bash
llvm-config --version  # Should be 14.x, 15.x, etc.
```

If you have multiple LLVM versions installed:

```bash
# macOS: remove old version
brew uninstall llvm@13

# Linux: switch version
sudo update-alternatives --config llvm-config
```

### Linker Errors (Undefined Reference)

**Error:**
```
undefined reference to 'llvm::Module::Module(...)' 
```

**Cause:** LLVM libraries not found during linking  
**Fix:**

1. Verify LLVM_CONFIG is set:
   ```bash
   echo $LLVM_CONFIG
   llvm-config --libs
   ```

2. Rebuild from scratch:
   ```bash
   cd build
   rm -rf CMakeCache.txt CMakeFiles/
   cmake ..
   make clean
   make
   ```

3. Check that LLVM libraries exist:
   ```bash
   llvm-config --libdir
   ls $(llvm-config --libdir)  # Should show .a or .so files
   ```

### Out of Memory During Build

**Error:**
```
error: virtual memory exhausted
```

**Fix:** Reduce parallel jobs:

```bash
make -j2   # Instead of -j$(nproc)
```

Or add swap space (on Linux):

```bash
sudo fallocate -l 4G /swapfile
sudo chmod 600 /swapfile
sudo mkswap /swapfile
sudo swapon /swapfile
```

### Permission Denied

**Error:**
```
cmake: Permission denied when writing to /usr/local/
```

**Fix:** Install to your home directory or use sudo:

```bash
cmake .. -DCMAKE_INSTALL_PREFIX=$HOME/.local/eskiu
```

Or use sudo (not recommended):

```bash
sudo cmake --install .
```

### "make: Command not found"

**macOS:**
```bash
xcode-select --install
```

**Linux:**
```bash
apt-get install build-essential  # Debian/Ubuntu
dnf install make gcc-c++         # Fedora
```

---

## Cleaning and Rebuilding

To do a clean rebuild from scratch:

```bash
cd build
rm -rf *  # Remove all build artifacts
cmake ..
make
```

Or from the root directory:

```bash
rm -rf build
mkdir build && cd build
cmake ..
make
```

---

## Installing Eskiu

To install Eskiu system-wide (optional):

```bash
cd build
sudo make install

# Verify
eskiu --version  # Should work from anywhere
```

To uninstall:

```bash
cd build
sudo make uninstall
```

To install to a custom location without sudo:

```bash
cmake .. -DCMAKE_INSTALL_PREFIX=$HOME/.local
make install

# Add to PATH
export PATH=$HOME/.local/bin:$PATH
```

---

## Next Steps

1. Run your first program: [QUICKSTART.md](../QUICKSTART.md)
2. Learn the language: [GETTING_STARTED.md](GETTING_STARTED.md)
3. Understand the compiler: [ARCHITECTURE.md](ARCHITECTURE.md)
4. Debug issues: [DEBUGGING.md](DEBUGGING.md)

---

## Getting Help

- Check the [FAQ](FAQ.md) for common questions
- Read [DEBUGGING.md](DEBUGGING.md) for compiler troubleshooting
- File a bug on GitHub with full build output: `cmake --build . 2>&1 | tee build.log`
