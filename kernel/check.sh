#!/bin/sh
# check.sh: boot kernel.elf in QEMU headless and check its UART output.
# Run through `make check`, which passes QEMU, QEMU_FLAGS and TIMEOUT.
# The kernel ends with PSCI SYSTEM_OFF, so QEMU exits by itself; the timeout only
# catches a hang.

QEMU=${QEMU:-qemu-system-aarch64}
QEMU_FLAGS=${QEMU_FLAGS:-"-M virt -cpu cortex-a57 -nographic -kernel kernel.elf"}
TIMEOUT=${TIMEOUT:-30}

out=$(mktemp)
trap 'rm -f "$out"' EXIT

# A watchdog instead of timeout(1), which macOS does not ship.
# shellcheck disable=SC2086
$QEMU $QEMU_FLAGS < /dev/null > "$out" 2>&1 &
qpid=$!
( sleep "$TIMEOUT"; kill "$qpid" 2> /dev/null ) &
wpid=$!
wait "$qpid"
status=$?
kill "$wpid" 2> /dev/null
wait "$wpid" 2> /dev/null

tr -d '\r' < "$out" > "$out.txt" && mv "$out.txt" "$out"
cat "$out"

fail=0
if [ "$status" -ne 0 ]; then
    echo "check: QEMU exited with status $status (timed out after ${TIMEOUT}s?)"
    fail=1
fi

# One extended regex per line; each must match a whole output line.
expected='Eskiu bare-metal kernel
Running at: EL1
Timer:      [0-9]+ Hz
Exception:  breakpoint \(EC 0x3C, IL 1, ISS 0x2A\)
  ELR_EL1:  0x[0-9A-F]{16}
  Recovered: resuming at the next instruction\.
Back from the breakpoint\.
UART base:  0x0000000009000000
Heap:       0x0000000040300000 - 0x0000000040400000
alloc\(64\): 0x0000000040300000
alloc\(4096\): 0x0000000040300040
alloc\(4194304\): out of memory
Heap used:  4160 bytes
Uptime:     [0-9]+ us
Powering off\.'

echo "$expected" | while IFS= read -r pat; do
    if ! grep -Eqx -- "$pat" "$out"; then
        echo "check: missing line matching: $pat"
        exit 1
    fi
done || fail=1

if [ "$fail" -ne 0 ]; then
    echo "check: FAILED"
    exit 1
fi
echo "check: OK"
