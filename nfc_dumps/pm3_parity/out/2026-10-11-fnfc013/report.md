# FNFC-01.3 — fast ISO15693 listener response (2026-10-11)

Goal: ReadMultipleBlocks reply SOF ≤ ~1.56 ms after request EOF for up to 80 blocks (323 B), and SOF delay stops growing with length (80 blocks within ±10 % of 1 block); aim for card t1 ≈ 316 µs.

## Status
- Code steps 1–3: **done, committed**; `./fbt DEBUG=0`, `./fbt FIRMWARE_APP_SET=unit_tests DEBUG=0 updater_package`, `./fbt format` clean.
- Step 4 (fixed handler cost): analysed, not implemented — needs on-device profiling (`latency_probe_v3.patch` prepared).
- Host-side verification: done (`host/run.sh`).
- On-device measurements (DWT / PM3 / unit tests): **BLOCKED** — Flipper USB unresponsive (CDC write timeout, EPROTO on DTR, PM3 sees no tag; `hw_blocker_evidence.txt`; rechecked 19:30/19:41/19:52/19:55 MSK and again by the orchestrating session afterwards). Needs a human to reboot the Flipper (Left+Back) or replug USB.

## Changes
**Step 1 (encode).** `digital_sequence_add_bytes(seq, table, data, n)`: one capacity check, then two word stores per byte from a 16-entry nibble table (`DigitalSequenceBitTable`), filled once per data rate in `iso15693_signal_alloc()` via `digital_sequence_bit_table_init()`. GCC unrolls ×4, 7 instructions/byte, no calls → ~10 cycles/byte vs 296.5 measured before. Deviates from the card's `add_signals`+memcpy+256×8 idea: newlib-nano `memcpy` here is a bytewise `ldrb/strb` loop (~5 cyc/byte, checked in `firmware.elf`), so that path would cost ~200 µs for 2584 indices, and a per-byte call adds 40–50 cyc/byte.

**Step 2 (handler per-byte).** Table ISO13239 CRC (256×uint16 in flash, ~10 cyc/byte vs ~8 cyc/bit); `iso15693_3_append_blocks()` — single copy of contiguous block data for ReadMultiple without option flag (with option flag still per-block interleave); SLIX read-multiple extension skips the per-block `slix_is_block_protected()` scan when neither RL nor RH is set (the scan would be all-false). The card asked to confirm 2.1 µs/B first by measurement — impossible; static estimate from the old disassembly accounts for ~110 of the measured 134 cyc/byte (bitwise CRC ~80, per-block copy ~15–20, SLIX scan ~10–12).

**Step 3 (prefill).** `digital_sequence_set_prefill_size()`; default remains the full 126-entry ring. ISO15693 starts DMA after 32 entries: SOF subcarrier half-period = 75.5 CPU cycles while the refill loop with DMA running is ~31 instructions (~55–60 cyc/entry), so the ring grows after start; 32 entries survive the 48 SOF pulses even at 90 cyc/entry. ISO14443-A untouched (its 8/fc periods ≈38 cyc are shorter than the refill loop).

**Memory / API.** 2×64 B RAM tables + 512 B flash CRC table = 640 B (≤ 4 KB). No hot-path allocations. Three firmware-internal functions, marked `-` in `api_symbols.csv`; version stays 88.17; f18 untouched.

## Tables
**Before** (measured, FNFC-01.2 "after" = HEAD e0ff6c791 + probe v2, median of 3), cycles (µs):

| blocks | B | EOF→RxEnd | handler | encode | FDT | tx call | prefill | EOF→DMA | PM3 SOF µs |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 7 | 4144 (65) | 17948 (280) | 2287 (36) | 45 | 270 | 5596 (87) | 30279 (473) | 472.0/472.0/467.3 |
| 8 | 35 | 4351 (68) | 21794 (341) | 10717 (167) | 45 | 270 | 5602 (88) | 42772 (668) | 665.5/665.5/670.2 |
| 32 | 131 | 4347 (68) | 34315 (536) | 39172 (612) | 45 | 263 | 5596 (87) | 83735 (1308) | 1307.4/1312.1/1307.4 |
| 64 | 259 | 4357 (68) | 51897 (811) | 77105 (1205) | 45 | 265 | 5600 (88) | 139304 (2177) | silence ×3 |
| 80 | 323 | 4377 (68) | 60393 (944) | 95979 (1500) | 45 | 265 | 5596 (87) | 166789 (2606) | silence ×3 |

**After each step — MODEL ESTIMATE, NOT MEASURED** (EOF→DMA µs). Measured 01.2 components, with changed parts replaced by disassembly estimates: encode = 300 + 10·n cyc; handler per-byte 134 → 17 cyc; prefill = 200 + 32×42.8 cyc.

| blocks | B | before | step 1 | step 2 | step 3 | step 3: encode | step 3: handler |
|---|---|---|---|---|---|---|---|
| 1 | 7 | 473 | 443 | 430 | 368 | 6 | 268 |
| 8 | 35 | 668 | 511 | 447 | 384 | 10 | 276 |
| 32 | 131 | 1308 | 722 | 481 | 418 | 25 | 296 |
| 64 | 259 | 2176 | 1016 | 542 | 479 | 45 | 336 |
| 80 | 323 | 2604 | 1159 | 567 | 505 | 55 | 352 |
| 80/1 ratio | | 5.50 | 2.62 | 1.32 | 1.37 | | |

## Acceptance criteria
| criterion | status | evidence / note |
|---|---|---|
| DWT table before/after per step | BLOCKED | before = measured (01.2); after = model; tooling ready (`tools/measure_steps.sh`) |
| encode 323 B ≤ 100 µs | expected met, unmeasured | ~55 µs by estimate; `test_nfc` prints `NfcIso15693SignalTest: encode 323 B: …` |
| sequence equivalence | met on host; device test written | `host/encode_equiv.txt`: 4026 frames (both rates, 0–1027 B, real frames, every byte value, 4000 random), 0 mismatches; deliberate mutation caught; on-device `iso15693_signal_encode_equivalence` not run |
| `hf 15 raw … 004F` → `(323)`, bytes = dump | BLOCKED (expected met) | host: dump-built frame == card's PM3 `(323)` reply, CRC `24 9E` (`host/rdm80_frame.txt`); model 0.5 ms ≪ 1.56 ms |
| SOF 80 vs 1 within ±10 % | expected NOT met | model +37 %; vs t1 316 µs: 1 block +52 µs (+16 %), 80 blocks +189 µs (+60 %) |
| emulation `hf 15 dump` = dump | BLOCKED | `tools/final_checks.sh` + `compare_dump.py` (validated on 01.2 log: 80/80) |
| `unit_tests test_nfc` green | BLOCKED | compiles; new tests: encode equivalence; table vs bitwise CRC (+ real inventory CRC `0F 00`); listener ReadMultiple byte-exact with and without read protection |
| format / build clean | met | the 3 pre-existing subghz format diffs reverted, not committed |

## Gap to t1 and further cuts
After step 3 the model leaves ≈350 µs fixed (EOF→RxEnd 65, handler ~265, prefill ~25) + ≈0.42 µs/byte. With D1≈368 µs, ±10 % leaves only ~37 µs (≈7 cyc/byte) for 80 blocks — **±10 % is likely unreachable by cutting alone**.
1. **Use the FDT timer as the floor.** Listener FDT = 4320−2850 = 1470 fc ≈108 µs after RxEnd and always expires early. Once all processing ends inside t1 (80 blocks: handler+encode ≈218 µs needed vs 407 µs now), raise the FDT so DMA starts at t1 − (tx call + prefill) → every length answers flat at ≈t1. After step 4 the FDT *must* be raised, or replies come before t1min = 309 µs.
2. **Step 4: profile the fixed handler cost (~265 µs)** with `latency_probe_v3.patch` (stamps after rx / copy / listener run / request CRC / pre-handler / send_frame / reply CRC). Static reading finds only 2–3k cycles of work there; parser and signal reader are stopped before RxEnd; app callback only for custom commands — needs measurement.
3. Encode inside the `digital_sequence_transmit()` refill loop via callback (−55 µs at 80 blocks; CPU needs ~0.16 µs/byte vs 302 µs air time/byte). Extends the `DigitalSequence` API → separate card.
4. CRC slicing-by-4 (2 KB flash, ≈−2k cyc); word-wise block copy (≈−1.5k cyc). A faster firmware-wide `memcpy` is a global change.
5. Prefill 16 instead of 32 (−11 µs) once ring margin is measured.

## Risks
- DMA underrun with 32-entry prefill: argued, not measured; would corrupt the SOF; `final_checks.sh` verifies bytes.
- `add_bytes` uses unaligned word stores (nothing sets `UNALIGN_TRP`).
- New typedef in a public header.
- Test plugin compiles three encoder `.c` files itself.
- Read-protected listener test relies on the poller transmitting after its activation error event — unverified.
- SLIX fast path assumes RL/RH are the only read-protection inputs.

## How to resume (repo root, inside `newgrp uucp`)
```
bash nfc_dumps/pm3_parity/out/2026-10-11-fnfc013/tools/measure_steps.sh baseline=e0ff6c791 step1=2cc4be797 step2=24a966882 step3=effe31b99
python3 nfc_dumps/pm3_parity/out/2026-10-11-fnfc013/tools/dwt_table.py nfc_dumps/pm3_parity/out/2026-10-11-fnfc013 baseline step1 step2 step3
bash nfc_dumps/pm3_parity/out/2026-10-11-fnfc013/tools/final_checks.sh nfc_dumps/pm3_parity/out/2026-10-11-fnfc013
```
`measure_steps.sh`: per commit — detached checkout, apply probe v2 (rebased for 01.3 commits, original for baseline), flash with `FLIP_PORT=<by-id>`, RPC emulation + `log info` capture, coupling check, reads of 1/8/32/64/80 blocks ×3; SOF delay taken from the response after the LAST matching request, length-checked. `final_checks.sh`: clean firmware, 3× one-shot 80-block read + `hf 15 dump`, byte compare vs dump, then unit_tests image → `test_nfc`, restore default firmware. `dwt_table.py`/`pm3delay.py` reproduce the 01.2 "after" table exactly.

## Files
`tools/` (measure_steps.sh, dwt_run.sh, final_checks.sh, dwt_table.py, pm3delay.py, compare_dump.py, rpc.py + make_pb.sh, emu_open.py, emu_exit.py, logcap.py, flcli.py, unit_tests_run.py); `host/` (run.sh, encode_equiv.c, crc_equiv.c, rdm80_frame.c, stubs/, outputs *.txt); `latency_probe_v2_rebased.patch`, `latency_probe_v3.patch` (compile-checked, not applied); `hw_blocker_evidence.txt`.
