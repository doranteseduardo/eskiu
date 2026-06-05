# Makefile — Eskiu v0.1 bare-metal kernel (ARM64, QEMU -M virt)
#
# Prerequisites (macOS):
#   brew install lld qemu
#
# Build:   make
# Run:     make run
# Clean:   make clean
# Setup:   make setup   (installs lld and qemu via Homebrew)

TARGET   = aarch64-unknown-none-elf
ESKIUC   = ../build/eskiuc
CLANG    = /opt/homebrew/Cellar/llvm/22.1.5/bin/clang
LLD      = ld.lld

ESKIU_FLAGS = --target $(TARGET) --freestanding
AS_FLAGS    = --target=$(TARGET)
LD_FLAGS    = -T linker.ld -nostdlib -static

OBJS = boot.o kernel.o

.PHONY: all run clean setup check-deps

all: check-deps kernel.elf

check-deps:
	@which $(LLD)      > /dev/null 2>&1 || (echo "error: ld.lld not found — run: brew install lld" && exit 1)
	@which qemu-system-aarch64 > /dev/null 2>&1 || (echo "warning: qemu not found — run: brew install qemu")

setup:
	brew install lld qemu

boot.o: boot.s
	$(CLANG) $(AS_FLAGS) -c $< -o $@

kernel.o: kernel.esk uart.esk alloc.esk
	$(ESKIUC) $(ESKIU_FLAGS) kernel.esk -o $@


kernel.elf: $(OBJS)
	$(LLD) $(LD_FLAGS) -o $@ $^
	@echo ""
	@echo "  kernel.elf built successfully"
	@llvm-objdump -d kernel.elf | head -30 || true

run: kernel.elf
	qemu-system-aarch64 \
	  -M virt \
	  -cpu cortex-a57 \
	  -nographic \
	  -kernel kernel.elf

run-window: kernel.elf
	osascript -e 'tell application "Terminal" to activate' \
	           -e 'tell application "Terminal" to do script "cd $(CURDIR) && make run"'

clean:
	rm -f *.o kernel.elf
