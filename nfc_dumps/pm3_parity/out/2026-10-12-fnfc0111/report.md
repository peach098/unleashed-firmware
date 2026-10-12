# FNFC-01.11: flat t1 reply latency for the ISO15693 listener (2026-10-12)

Goal: the ISO15693 listener's reply SOF starts 316 ± 10 µs after the request EOF for 1/8/32/64/80 blocks (like the real
SLIX2 card), with 80/1 within ±5 % and no reply before t1min = 4192/fc ≈ 309 µs.

## Result in short
- **Reset fix.** Both full `bit_buffer_reset(tx_buffer)` stages are gone (5 call sites → size-only reset):
  - run→crc: 7922 → **520** cycles
  - crc→handler: 7310 → **324** cycles
  - RxEnd→tx_enter: 1 block 264 → 36 µs, 80 blocks 370 → 145 µs
- **Listener FDT.** `FURI_HAL_NFC_ISO15693_LISTENER_FDT_COMP_FC` 2850 → **1385 fc**, calibrated on the device.
  - `ISO15693_SIGNAL_PREFILL_SIZE` 32 → **16** (−9.5 µs). Card requirement 5: cut the prefill first.
  - `ISO15693_3_FDT_LISTEN_FC` is unchanged, so the card's STOP condition does not apply.
- **PM3 SOF on the clean release build `b9673b213`** (10 repeats per length):
  - median **316.2 µs** for 1/8/32/80 blocks and 320.9 µs for 64
  - **minimum over all 50 repeats 311.5 µs**, maximum 325.7 µs
  - **80/1 = 1.000** (before: 368 → 533 µs, ratio 1.449)
- **FDT wait at 80 blocks** (probe build, same constant): 535–968 cycles (8.4–15 µs), median 12.2 µs. The timer was never missed in 7 samples. At 1 block the FDT wait is 177 µs.
- **Reads, all compared with the dump** (same-day 01.10 firmware for reference: 8/10 on `004F`):
  - `004F` → `(323)`: 8/10 one-shot, 9/10 in the DWT set
  - `001F`: 3/3 one-shot, 10/10 in the set
  - `hf 15 reader --all`: 3/3
  - `hf 15 dump`: **3/3 complete, 80/80 equal, no breakoffs** (also 3/3 in the 1345 final run)
  - ISO14443-A: `hf 14a info` 3/3; `rdbl`/`rdsc` equal the file
- **`test_nfc` 56/56**, twice, on the final code (55 existing + 1 new).
- **1-of-256: not measurable.**
  - The listener never answers a 1-of-256 request (0/5).
  - This was already so before this branch: the parser's mode detection is never enabled (`furi_hal_nfc_iso15693_detect_mode()` has no in-tree caller), so it decodes only 1-of-4.
  - Measuring t_rx in that mode would need a code change, which is out of scope.

## Branch and commits (`fnfc-01.11-fdt-t1` from `origin/fnfc-01.10-tick-arithmetic` 61a37228e, pushed after each step)
| commit | content |
|---|---|
| `727b29c11` | tools: `measure.sh` with probe v3 + `NREP`, `series.sh`, `sof_table.py` |
| `469582ac2` | unit_tests: `iso15693_3_listener_short_after_long_test` (written first) |
| `3b555a1d7` | nfc: iso15693_3, slix listeners: size-only reset of the tx buffer (5 places) |
| `7c2b37463` | furi_hal: FDT comp 2850 → 1400 fc (model value; the commit message says so) |
| `3a2112cfb` | unit_tests: explicit `request` initializers (the test plugin builds with `-Werror=missing-field-initializers`) |
| `4255ff2df` | furi_hal: FDT comment: prediction vs measured; t_tx 4.2 + 30.3 = 34.5 µs; DEBUG=1 note |
| `739782e4c` | digital_signal: ISO15693 prefill 32 → 16 |
| `06c3e7c0b` | furi_hal: FDT 1400 → 1345 fc; comment with the measured terms |
| `b9673b213` | furi_hal: FDT 1345 → 1385 fc (the release build lands one PM3 sample later than the probe build) |
| `d3db30499` … | nfc_dumps: measurements, tables, blocker evidence, this report |

- Code diff vs 61a37228e touches 6 files: `nfc_test.c`, `iso15693_signal.c`, `iso15693_3_listener.c`, `iso15693_3_listener_i.c`, `slix_listener_i.c`, `furi_hal_nfc_iso15693.c`.
- `api_symbols.csv` is unchanged (88.17).

**Unit-test image.** Built from local history, because the remote `unit_tests` app set does not link (4 undefined `js_*`, pre-existing).
- Worktree `.claude/worktrees/fnfc0110-ut`, branch `fnfc0111-ut-local`, not pushed.
- `be0ea1c4a` = `f5e110ca6` + cherry-picks of all the code commits above.
- The listener, signal, HAL and signal-test files are byte-identical to the push branch.

## Code change
### 1. Size-only reset (`bit_buffer_set_size(buf, 0)`, same idiom as `mf_ultralight_listener.c:931`)
Call sites:
- `iso15693_3_listener.c:68`: on RxEnd, before the CRC check
- `iso15693_3_listener_i.c:659,673`: reply / error reply
- `slix_listener_i.c:605,620`: reply / error reply

Why it is safe (grep + review of every `bit_buffer_*` call on these tx buffers):
- **Writers** are only `append_byte` and `append_bytes`, called directly, via `iso15693_3_append_uid/_block/_blocks/_block_security`, and via `iso13239_crc_append`. They write whole bytes at `data[size_bytes]` and never read old content.
- **Not used** on a listener tx buffer: `append_bit`, `set_bit`, `set_byte`, and writing through `get_data` (`get_data` is only used on rx buffers).
- **Readers** use the size only:
  - `bit_buffer_copy()` in `send_frame` copies `size_bytes` (and returns at once when copying into itself)
  - the CRC
  - `nfc_listener_tx()`
- **No other code** writes into these buffers.
- **Test:** the new test checks the exact length of each reply.

### 2. Listener FDT (`targets/f7/furi_hal/furi_hal_nfc_iso15693.c`, comment next to the constant)
```
EOF->DMA = t_rx + (4320 - COMP)/fc + t_timer + t_tx   =>   COMP = 4320 - (t1 - t_rx - t_timer - t_tx) * fc
t_rx    = EOF -> RxEnd (worker)                       65..69 us, median 68 us
t_timer = timer ends this much later than requested   6.7 us  (14211 cycles from RxEnd for 2920 fc requested)
t_tx    = timer expiry -> first DMA                   4.2 us call + 20.9 us prefill (16 entries) = 25.1 us
t1      = EOF->DMA 319 us  (card PM3 SOF 316 us; the PM3 bins the Flipper SOF 0..4.7 us before EOF->DMA)
COMP    = 4320 - (319 - 68 - 6.7 - 25.1) us * 13.56 MHz = 1347
          measured with 1345: probe build 316.2 us, release build 320.9 us (one PM3 sample later)  ->  1385
```
- The comment also says DEBUG=1 builds are expected to miss the timer for long replies. Those replies then start late (after t1), not early. This is not measured.
- `nfc_iso15693_listener_tx_sof()` waits on the same timer.

### 3. Prefill 16
- The 01.3 argument still holds:
  - SOF and data subcarrier edges are at least ~75 cycles apart.
  - The refill loop takes ~55–60 cycles per entry.
  - So the ring only grows once the DMA runs.
- Prefill went from 1938 to 1330 cycles.
- 1/8/32-block replies decode 10/10 on the clean build, and the dump is 3/3 with 80/80 equal.
- There is no sign of underrun:
  - an underrun would truncate the frame, which the PM3 reports as `timeout`;
  - every failure seen was `command failed`.

### 4. Unit test `iso15693_3_listener_short_after_long_test`
- It reuses the 01.3 ReadMultipleBlocks harness, which now takes a request kind (read multi / read single / SLIX GET_RANDOM) and a case list.
- Read protection is on. Every 61 B reply is followed by a shorter one:
  - single read 5 B
  - single read with block security 6 B
  - error reply 2 B
  - GET_RANDOM 3 B (SLIX buffer)
  - flags only 1 B
  - then 5 B after 1 B
- The reply length must match exactly.
- The old code passes it too, so it is a regression guard, not a red test. It passes on the device (56/56).

## Tables
### DWT, probe v3, medians in µs (64 cycles = 1 µs)
Builds: `before` = 01.10 firmware; `reset` = size-only reset with the old FDT; `cal2` = final code. Empty cells were not tabulated for that build.

| build | blocks | EOF→RxEnd | handler | encode | FDT wait | tx call | prefill | **EOF→DMA** |
|---|---|---|---|---|---|---|---|---|
| before | 1 / 8 / 32 / 64 / 80 | 64 / 68 / 68 / 68 / 68 | 264 / 276 / 307 / 348 / 370 | 6 / 11 / 29 / 52 / 63 | 1 / 1 / 1 / 1 / 1 | 4 | 30 | **370 / 391 / 439 / 506 / 539** |
| reset (FDT 2850) | 1 / 8 / 32 / 64 / 80 | 65 / 68 / 68 / 68 / 69 | 36 / 46 / 79 / 130 / 152 | 6 / 11 / 29 / 52 / 63 | 72 / 58 / 7 / 1 / 1 | 4 | 30 | **215 / 218 / 218 / 289 / 319** |
| after (1400, prefill 32) | | 65 / 68 / 68 / 68 / 69 | 36 / 46 / 79 / 123 / 145 | | 179 / 165 / 114 / 47 / 11 | 4 | 30 | 321 / 325 / 325 / 325 / 327 |
| pf16 (1400, prefill 16) | | | | | 179 / 165 / 114 / 44 / 14 | 4 | 21 | 312 / 315 / 315 / 315 / 316 |
| cal (1345) | | | | | 183 / 169 / 118 / 51 / 18 | 4 | 21 | 316 / 319 / 319 / 319 / 320 |
| **cal2 (1385, final)** | 1 / 8 / 32 / 64 / 80 | 65 / 68 / 68 / 68 / 69 | 36 / 46 / 79 / 123 / 145 | 6 / 11 / 29 / 52 / 63 | **180 / 166 / 115 / 48 / 12** | 4 | 21 | **313 / 316 / 316 / 317 / 317** |

- **PROBE3 breakdown** (`*_probe3_table.md`), before vs reset/cal2:
  - run→crc: 7908–7922 → 506–520 cycles
  - crc→handler: 7310 → 324 cycles
  - the other stages are unchanged; send→crc_app (the reply CRC) is 84 µs at 80 blocks
- **FDT wait per sample at 80 blocks:**
  - with 1385: min 535, max 968 cycles; all 7 were before the timer expired
  - with 1400 and prefill 32: 39..912 cycles, and **1 of 7 missed the timer** (which is why the prefill was cut)

### PM3 SOF delay (µs; the response after the last matching request, length- and data-checked, `*_sof_table.txt`)
| build | n per length | 1 | 8 | 32 | 64 | 80 | min (all) | max | 80/1 |
|---|---|---|---|---|---|---|---|---|---|
| card (01.1) | | 316 | 316 | 316 | 316 | 316 | | | 1.00 |
| before (01.10 fw, probe) | 7 | 368.1 | 387.0 | 434.2 | 505.0 | 533.3 | 363.4 | 542.8 | 1.449 |
| reset (FDT 2850, probe) | 7 | 212.4 | 212.4 | 212.4 | 287.9 | 316.2 | **207.7** | 320.9 | 1.489 |
| after (1400, probe) | 7 | 320.9 | 320.9 | 320.9 | 320.9 | 325.7 | 316.2 | 330.4 | 1.015 |
| pf16 (1400, probe) | 5 | 311.5 | 311.5 | 311.5 | 311.5 | 311.5 | 311.5 | 320.9 | 1.000 |
| cal (1345, probe) | 7 | 316.2 | 316.2 | 316.2 | 316.2 | 316.2 | 311.5 | 320.9 | 1.000 |
| final (1345, **release**) | 10 | 320.9 | 320.9 | 320.9 | 320.9 | 320.9 | 316.2 | 330.4 | 1.000 |
| cal2 (1385, probe) | 7 | 311.5 | 316.2 | 316.2 | 316.2 | 311.5 | **306.8** | 320.9 | 1.000 |
| **final2 (1385, release)** | 10 | **316.2** | **316.2** | **316.2** | **320.9** | **316.2** | **311.5** | 325.7 | **1.000** |

Notes:
- PM3 values come in 64 fc (4.72 µs) steps: 306.8, 311.5, 316.2, 320.9, 325.7, …
- With the same constant, the probe build lands about one step earlier than the release build. This happened at both 1345 and 1385.
- The 1-block read is about 3 µs earlier than the others because its EOF→RxEnd is 65 µs, not 68 µs.

### PM3 decode success, release builds (one-shot `hf 15 raw`, each reply compared with the dump)
| build | 001F (131 B) | 003F (259 B, DWT set) | 004F (323 B) |
|---|---|---|---|
| final (1345) | 3/3, set 10/10 | 9/10 | one-shot 3/3, set 4/10 |
| **final2 (1385)** | **3/3**, set 10/10 | 10/10 | **8/10** one-shot, set 9/10 |
| 01.10 firmware `727b29c11`, same day (`env0110`) | | | 8/10 |

The long-reply misses do not depend on this change:
- The same-day 01.10 firmware also gets 8/10.
- Every miss is `command failed`, and the frame was transmitted: the probe logs a TX for every request.
- This is the PM3 decode window found in 01.10: about 17 ppm margin at 323 B, so it is sensitive to crystal offset and drift.
- The 4/10 in `final` came in one burst; `final2` did not repeat it (9/10 + 8/10).

## Acceptance criteria
| criterion | status | evidence |
|---|---|---|
| DWT before/after for 1/8/32/64/80; run→crc and crc→handler ≤ 1000 cycles | **met** | 7922 → 520, 7310 → 324 (`before` / `cal2`) |
| PM3 SOF 316 ± 10 µs for all five lengths | **met** | release: 316.2 / 316.2 / 316.2 / 320.9 / 316.2 µs (medians) |
| minimum ≥ 309 µs | **met on the release build: 311.5 µs** (50 repeats); **the probe build once showed 306.8** (1 block) | see the margin note in (b) |
| 80/1 within ±5 % | **met** | 1.000 (release and probe) |
| FDT wait > 0 for all lengths | **met** | 80 blocks min 8.4 / median 12.2 µs (probe build, 1385); 1 block 177 µs |
| `004F` (323) = dump ≥ 2/3 | **met** | 8/10 one-shot, 9/10 set |
| `001F` (131) = dump 3/3 | **met** | 3/3 |
| `hf 15 reader --all` sees the UID | **met** | 3/3 |
| `hf 15 dump` complete runs 80/80; breakoffs recorded | **met** | 3/3 + 3/3 (1345); no breakoffs in 480 single-block reads |
| new unit test + all listener tests; `test_nfc` green on the device | **met** | 56/56 × 2 (`unit_tests_final_run{1,2}.log`, `be0ea1c4`) |
| FDT comment with the formula; api_symbols unchanged; format and builds clean | **met** | DEBUG=0: 0 warnings; unit_tests image builds; `./fbt format` changed only the 3 subghz files (reverted) |
| commits pushed; report.md | **met** | |

## Review points (a)–(e)
- **(a) FDT commit and comment.**
  - The pushed FDT commit is `7c2b37463`; it was `0b9f66f50` before an amend, and the amend happened before the push. Pushed history is not rewritten; the fixes are follow-up commits:
    - `4255ff2df`: t_tx made consistent (4.2 + 30.3 = 34.5 µs), and the comment says which parts are prediction.
    - `06c3e7c0b` / `b9673b213`: the model is replaced by measured terms and a calibrated constant.
- **(b) Margin to t1min.**
  - Minimum SOF over all repeats on the clean release build: **311.5 µs** (10 per length). That is one PM3 step (2.4 µs) above t1min. A step covers the 4.7 µs below it, so the true margin is probably about 2–7 µs.
  - The probe build of the same constant once showed **306.8 µs** at 1 block.
  - The timer is the same for every length, so the 1-block case cannot be raised alone.
  - **1-of-256: not feasible.** The listener does not decode 1-of-256 at all (0/5 replies; mode detection is never enabled). This was already the case before this branch, so t_rx in that mode cannot be measured without a code change.
- **(c) 80-block FDT wait.** With 1385: **535..968 cycles = 8.4..15.1 µs**, median 12.2 µs (probe build, 7 samples). The DEBUG=1 line is in the comment.
- **(d) Follow-ups, proposed, not fixed:**
  1. **Silent paths now hold the timer about twice as long.** The timer is started on every RxEnd, including requests that get no reply: non-addressed out-of-range reads, unknown commands, wrong CRC, a single EOF.
     - It now runs about 223 µs; it used to run about 115 µs.
     - `furi_hal_nfc_timer_start_core_ticks()` calls `furi_check(!running)` at `furi_hal_nfc_timer.c:137`, so a second RxEnd while the timer is still running crashes the Flipper. The window for that crash has roughly doubled.
     - A reader's next frame normally follows a t2/t3 gap, so this should not happen in practice. It was not tested with back-to-back frames on either the old or the new window.
     - Fix options: start the timer only when a reply is due, or stop it on silent paths.
  2. **The exported `nfc_iso15693_listener_tx_sof()` now fires later.** It waits on the same timer, so it fires about 223 µs after RxEnd instead of about 115 µs. That is t1 for SOF-only answers, but any out-of-tree caller relying on the earlier timing will see different behaviour.
- **(e) Stale hash in this report:** fixed (see the commits table).

## Observations / risks
- **Probe vs release build.** With the same constant, the release build's PM3 SOF is about 4 µs later than the probe build's. The cause is unknown, probably code layout or cache affecting t_rx (the thread wake-up). The constant is tuned for the release build.
- **Pre-existing single-EOF bug, unchanged.** After a write with the option flag, the single-EOF reply goes out after `iso15693_3_listener_run()` has already reset `tx_buffer`. The reader gets an empty payload + CRC instead of `00` + CRC. The size-only reset keeps this behaviour byte for byte. Worth a separate card.
- **80-block margin is about 8 µs.** The next cuts would be the reply CRC in `send_frame` (84 µs at 80 blocks; table CRC from flash, about 16.8 cycles/byte) or the encoding.
- **PM3 decode window at 64/80 blocks** limits `004F` success (as in 01.10). Latency does not.
- **Hardware incidents during this session** (`hw_blocker_evidence.txt`):
  - Flipper USB CDC hung from about 10:41 to 11:31; the user replugged it.
  - The PM3 did not answer from about 11:40 to 12:00, then recovered by itself.
  - The first `unit_tests` flash failed: the test plugin did not compile (missing field initializers under `-Werror`). Fixed in `3a2112cfb`. The "builds" claim in the provisional report had covered only the firmware, not the plugin.

## How to reproduce (repo root, inside `newgrp uucp`)
```
O=nfc_dumps/pm3_parity/out/2026-10-12-fnfc0111; D="nfc_dumps/SLIX2_2026-02-27_16,08,34.nfc"
DWT=1 NREP=7 N32=0 N64=0 N80=0 bash $O/tools/measure.sh $O before=727b29c11:v3 reset=3b555a1d7:v3 cal2=HEAD:v3
python3 nfc_dumps/pm3_parity/out/2026-10-11-fnfc013/tools/dwt_table.py $O before reset after pf16 cal cal2
python3 $O/tools/sof_table.py $O "$D" before reset after pf16 cal final cal2 final2
DWT=1 NREP=10 N32=3 N64=0 N80=10 NDUMP=3 bash $O/tools/final.sh $O final2
bash $O/tools/rd256.sh $O r256 5
```
`measure.sh` gotchas:
- It must measure commits that contain the 01.11 directory, because the output goes there.
- Files that are tracked in one measured commit and untracked in another block the detached checkout. Measure at the branch HEAD, or move the results aside first.
