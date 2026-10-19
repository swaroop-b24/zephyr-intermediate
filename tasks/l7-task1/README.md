
# L7 Task 1 - Isolate the Command Parser

## Goal

A network command parser has an out-of-bounds read (`get=<n>` is never
checked against the settings table size). An attacker can use it to read
the device key. Contain the bug without modifying `parser.c` or
`credentials.c`.

## Changes (only two)

1. `prj.conf`: `CONFIG_USERSPACE=y`
2. `src/main.c`: the parser thread is created with `K_USER` instead of `0`

```c
k_thread_create(&parser_thread, parser_stack, PARSER_STACK_SIZE,
                parser_entry, (void *)attack_index, NULL, NULL,
                5, K_USER, K_NO_WAIT);
```

## Why it works

A user thread runs unprivileged and the MPU limits it to its own stack,
flash (code and const data) and memory partitions it was granted. The
settings table is `const`, so it is in flash and still readable, which is
why `get=0` works. `device_key` is ordinary RAM owned by the kernel, so
reading it faults.

## Results (QEMU mps2/an521/cpu0)

| | Starter | Isolated |
|---|---|---|
| `get=0` | `led=off` | `led=off` |
| first attack | prints `EC-PRIVATE-KEY:8` | MPU fault, Data Access Violation |
| second attack | prints `f3a91c25d7e04b6` | never runs (system halts) |
| key leaked | yes, in full | no |

The attack index differs between the two builds (41941699 in the starter,
41939990 isolated) because enabling userspace changes the memory layout.
`main.c` computes it from the real addresses, so it still targets the key.

## Verification

`logs/verification.txt` shows the fault address equals the key address:

- `nm` on the isolated build: `38000040 d device_key`
- QEMU log: `MMFAR Address: 0x38000040`
- occurrences of `EC-PRIVATE` in the isolated run: 0

Other logs: `logs/baseline-run.txt` (key leaked), `logs/fixed-run.txt`
(MPU fault).

## Notes and limits

- The bug is still there. Isolation limits what it can reach, it does not
  fix it. A bounds check in `parser.c` is still the real fix.
- Flash is readable by user threads. Secrets stored in `const` data would
  not be protected by this. The key only needs to be in RAM, outside any
  partition granted to the parser.
- The default fatal-error handler halts the whole system on the fault. A
  product would handle the fault (log it, abort the parser thread or
  reset) instead of halting.
- Built natively with Zephyr 4.4.0 and SDK 1.0.1, not the instructor's
  Docker image with Zephyr 3.7.2. The behaviour is the same, and the boot
  banner shows v4.4.0.

## Build and run

```bash
west build -p always -b mps2/an521/cpu0 -d build_l7-fixed tasks/l7-task1
qemu-system-arm -machine mps2-an521 -cpu cortex-m33 -m 16 -nographic -vga none \
  -device loader,file=build_l7-fixed/zephyr/zephyr.elf
```
