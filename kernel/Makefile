# Makefile: Eskiu bare-metal kernel (ARM64, QEMU -M virt)
#
# Prerequisites (macOS):
#   brew install lld qemu
#
# Build:   make
# Run:     make run     (interactive; the kernel powers QEMU off when it is done)
# Check:   make check   (headless boot with a timeout, checks the UART output)
# Clean:   make clean
# Setup:   make setup   (installs lld and qemu via Homebrew)
#
# Every tool is overridable: make check ESKIUC=... CLANG=... LLD=... QEMU=...

TARGET  ?= aarch64-unknown-none-elf
ESKIUC  ?= ../build/eskiuc
CLANG   ?= clang
LLD     ?= ld.lld
QEMU    ?= qemu-system-aarch64
TIMEOUT ?= 30

# No FP/SIMD code: the trap frame in vectors.s saves only the general registers.
ESKIU_FLAGS = --target $(TARGET) --freestanding --mattr=-fp-armv8,-neon
AS_FLAGS    = --target=$(TARGET)
LD_FLAGS    = -T linker.ld -nostdlib -static
QEMU_FLAGS  = -M virt -cpu cortex-a57 -nographic -kernel kernel.elf

OBJS = boot.o vectors.o kernel.o
SRCS = kernel.esk uart.esk cpu.esk alloc.esk trap.esk

.PHONY: all run check clean setup check-deps

all: check-deps kernel.elf

check-deps:
	@command -v $(LLD) > /dev/null 2>&1 || (echo "error: $(LLD) not found (brew install lld)" && exit 1)
	@command -v $(QEMU) > /dev/null 2>&1 || echo "warning: $(QEMU) not found (brew install qemu)"

setup:
	brew install lld qemu

%.o: %.s
	$(CLANG) $(AS_FLAGS) -c $< -o $@

kernel.o: $(SRCS)
	$(ESKIUC) $(ESKIU_FLAGS) kernel.esk -o $@

kernel.elf: $(OBJS) linker.ld
	$(LLD) $(LD_FLAGS) -o $@ $(OBJS)

run: kernel.elf
	$(QEMU) $(QEMU_FLAGS)

check: check-deps kernel.elf
	QEMU="$(QEMU)" QEMU_FLAGS="$(QEMU_FLAGS)" TIMEOUT="$(TIMEOUT)" ./check.sh

run-window: kernel.elf
	osascript -e 'tell application "Terminal" to activate' \
	           -e 'tell application "Terminal" to do script "cd $(CURDIR) && make run"'

clean:
	rm -f *.o kernel.elf
