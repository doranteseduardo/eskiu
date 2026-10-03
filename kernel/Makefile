# Makefile: Eskiu bare-metal kernel (ARM64, QEMU -M virt)
#
# Prerequisites (macOS):
#   brew install lld qemu
#
# Build:   make         (OPT=2 for an -O2 build; the default is -O0)
# Run:     make run     (interactive; the kernel powers QEMU off when it is done)
# Check:   make check   (headless boot of an -O0 and an -O2 build, checks the UART output)
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
OPT     ?= 0

# No FP/SIMD code, at any -O level: the trap frame in vectors.s saves only the
# general registers, and nothing turns FP/SIMD on (CPACR_EL1.FPEN), so the first
# FP or vector instruction would trap. -O2 uses them for copies when allowed.
ESKIU_FLAGS = --target $(TARGET) --freestanding --mattr=-fp-armv8,-neon -O$(OPT)
AS_FLAGS    = --target=$(TARGET)
LD_FLAGS    = -T linker.ld -nostdlib -static
QEMU_FLAGS  = -M virt,gic-version=2 -cpu cortex-a57 -nographic -kernel $(ELF)

# Each -O level gets its own kernel object and ELF.
KOBJ = kernel-O$(OPT).o
ELF  = kernel-O$(OPT).elf
OBJS = boot.o vectors.o $(KOBJ)
SRCS = kernel.esk uart.esk cpu.esk alloc.esk trap.esk gic.esk timer.esk sched.esk \
       workers.esk shell.esk

.PHONY: all run check check-one clean setup check-deps

all: check-deps $(ELF)

check-deps:
	@command -v $(LLD) > /dev/null 2>&1 || (echo "error: $(LLD) not found (brew install lld)" && exit 1)
	@command -v $(QEMU) > /dev/null 2>&1 || echo "warning: $(QEMU) not found (brew install qemu)"

setup:
	brew install lld qemu

%.o: %.s
	$(CLANG) $(AS_FLAGS) -c $< -o $@

$(KOBJ): $(SRCS)
	$(ESKIUC) $(ESKIU_FLAGS) kernel.esk -o $@

$(ELF): $(OBJS) linker.ld
	$(LLD) $(LD_FLAGS) -o $@ $(OBJS)

run: $(ELF)
	$(QEMU) $(QEMU_FLAGS)

check: check-deps
	$(MAKE) check-one OPT=0
	$(MAKE) check-one OPT=2

check-one: $(ELF)
	QEMU="$(QEMU)" QEMU_FLAGS="$(QEMU_FLAGS)" TIMEOUT="$(TIMEOUT)" ./check.sh

run-window: $(ELF)
	osascript -e 'tell application "Terminal" to activate' \
	           -e 'tell application "Terminal" to do script "cd $(CURDIR) && make run"'

clean:
	rm -f *.o *.elf
