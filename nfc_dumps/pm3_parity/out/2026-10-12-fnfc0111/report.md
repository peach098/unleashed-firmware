# FNFC-01.11: flat t1 reply latency for the ISO15693 listener (2026-10-12)

**Status: code done, built, pushed. Every on-device step is BLOCKED: the Flipper stopped answering on USB
before step 0 could be flashed (`hw_blocker_evidence.txt`). A human has to reboot it (hold LEFT + BACK
for about 5 s, or replug it). After that, the measurements below can run unattended (see "How to resume").**
None of the hardware acceptance criteria (DWT, PM3 SOF, `hf 15 raw`, `hf 15 dump`, `test_nfc`) has been
measured. Numbers in this report marked *model* are predictions from the 01.3/01.10 DWT data, not results.

## Branch and commits (`fnfc-01.11-fdt-t1`, from `origin/fnfc-01.10-tick-arithmetic` 61a37228e, pushed)
| commit | content |
|---|---|
| `727b29c11` | nfc_dumps: tools (01.10 `measure.sh`/`series.sh` + probe v3, `NREP`; `sof_table.py`) |
| `469582ac2` | unit_tests: `iso15693_3_listener_short_after_long_test` (written first) |
| `3b555a1d7` | nfc: iso15693_3, slix listeners: size-only reset of the tx buffer (5 places) |
| `0b9f66f50` | furi_hal: `FURI_HAL_NFC_ISO15693_LISTENER_FDT_COMP_FC` 2850 → 1400 fc, formula in the comment |
| `d3db30499`, `e95f6f79d` | nfc_dumps: probe v3 rebased on the new code, blocker evidence, `final.sh` + `hf 15 reader --all` |

Unit-test image (local history, as in 01.10, because the remote `unit_tests` app set does not link: 4 undefined `js_*`):
worktree `.claude/worktrees/fnfc0110-ut`, local branch `fnfc0111-ut-local` = `f5e110ca6` + the three code
commits (`f7f0a44f3`, `60bfb961a`, `8b0608181`). It builds (`FIRMWARE_APP_SET=unit_tests DEBUG=0`, 0 warnings),
not run.

## Code change
### 1. Size-only reset (`bit_buffer_set_size(buf, 0)`, the idiom already in `mf_ultralight_listener.c:931`)
- `iso15693_3_listener.c:68` (RxEnd, before the CRC check), `iso15693_3_listener_i.c:659,673` (reply / error reply),
  `slix_listener_i.c:605,620` (reply / error reply). `bit_buffer_reset()` is not used in either listener any more.
- **Why it is safe (grep + review).** All `bit_buffer_*` calls on these tx buffers:
  - writers: `append_byte`, `append_bytes` (directly, and in `iso15693_3_append_uid/_block/_blocks/_block_security`),
    `iso13239_crc_append` (`append_bytes`). Each writes whole bytes at `data[size_bytes]` and moves the size.
    None reads or ORs old content.
  - `append_bit` / `set_bit` / `set_byte` / `get_data`-and-write: not used on a listener tx buffer (`get_data` only on the
    rx buffer, `iso15693_3_listener_i.c:784,887`, `slix_listener_i.c:578`). Parity is never used by ISO15693.
  - readers: `bit_buffer_copy()` in `send_frame` copies `size_bytes` only and returns at once when copying into itself;
    `iso13239_crc_append` and `nfc_listener_tx()` read `size` bytes only; `get_capacity_bytes` is independent of content.
  - No other code writes into these buffers (`Iso15693_3Listener` is used only by the SLIX listener; it has its own buffer).
  So every transmitted byte was written after the reset, and the frames are byte-for-byte the same.
- Expected effect (*model*, 01.3 PROBE3): run→crc 7916 → ~100 cycles, crc→handler 7317 → ~120 cycles
  (the rest of each stage is the CRC check and the flags/UID checks); handler −238 µs.

### 2. Listener FDT
`targets/f7/furi_hal/furi_hal_nfc_iso15693.c`: `FURI_HAL_NFC_ISO15693_LISTENER_FDT_COMP_FC` **2850 → 1400 fc**.
`ISO15693_3_FDT_LISTEN_FC` (4320, shared with the poller) is unchanged, so the STOP condition of the card does not apply.
```
EOF->SOF = t_rx + (4320 - COMP)/fc + t_tx   =>   COMP = 4320 - (t1 - t_rx - t_tx) * fc
t_rx = EOF -> RxEnd 68 us, t_tx = timer expiry -> first DMA (tx call 4 us + prefill 30 us) = 34.5 us,
t1 = 318 us EOF->DMA (= card's 316 us PM3 SOF delay + ~2 us; PM3 SOF ~ EOF->DMA - 2..3 us in 01.10 postdwt)
COMP = 4320 - 215.5 us * 13.56 MHz = 1398 -> 1400 fc; timer = 2920 fc = 215 us after RxEnd
```
*Model* margin at 80 blocks: handler after fix ≈ 139 µs + encode 63 µs ≈ 202 µs after RxEnd < 215 µs, i.e. FDT wait
≈ 13 µs (> 0). 1 block: ready ≈ 30 µs after RxEnd, FDT wait ≈ 185 µs. Every length should then start DMA at
≈ 318 µs and the PM3 SOF at ≈ 316 µs; jitter comes from EOF→RxEnd (65..71 µs observed), so ±4 µs is expected.
If the 80-block FDT wait comes out ≤ 0, the next cut is `send→crc_app` (reply CRC, 84 µs at 80 blocks) / prefill.

`nfc_iso15693_listener_tx_sof()` waits on the same timer, so it moves into the t1 window with the same constant
(no in-tree caller; checked only via `hf 15 reader --all`, to be run).

### 3. Unit test `iso15693_3_listener_short_after_long_test`
The ReadMultipleBlocks harness of 01.3 (poller ↔ listener over the mock, response compared byte for byte with a frame
built from the dump) got a request kind (read multi / read single / SLIX GET_RANDOM_NUMBER) and a case-list parameter.
The new case list (read protection low on, so the error path is reached) alternates 61 B replies with shorter ones:
read single 5 B, 12 blocks + security 61 B → read single + security 6 B, protected read → error reply 2 B,
GET_RANDOM 3 B (SLIX buffer, copied in `send_frame`), flags only 1 B, then 5 B after 1 B. Length must match exactly,
so any tail from the previous reply fails the case. The existing two read-multi tests keep their case lists.
TDD note: the old code also passes (it zeroed everything), so this test is a regression guard, not a red test;
the unit tests only run on the device.

## Build / format
- `./fbt DEBUG=0` (default app set) on the push branch: rc 0, 0 warnings; `API version 88.17 is up to date`;
  `targets/f7/api_symbols.csv` unchanged.
- `./fbt format`: reformatted the new test (folded into its commit) and the 3 known subghz files (reverted).
- `FIRMWARE_APP_SET=unit_tests DEBUG=0` on `fnfc0111-ut-local`: rc 0, 0 warnings.

## Acceptance criteria
| criterion | status |
|---|---|
| DWT before/after 1/8/32/64/80; run→crc, crc→handler ≤ 1000 cycles | **not measured** (Flipper hung) |
| PM3 SOF 316 ± 10 µs for all lengths, min ≥ 309 µs, 80/1 ± 5 % | **not measured**; constant from model only |
| FDT wait > 0 for all lengths | **not measured**; model: 13 µs at 80 blocks |
| `004F` → (323) ≥ 2/3, `001F` → (131) 3/3, `hf 15 reader --all` sees UID | **not measured** |
| `hf 15 dump` complete runs 80/80; breakoffs recorded | **not measured**, no breakoff data for 01.12 |
| new unit test + listener tests, `test_nfc` green on device | **written and built, not run** |
| FDT comment with formula; api_symbols unchanged; format/builds clean | **met** |
| commits pushed; report.md | **met** (this file, provisional) |

## How to resume (repo root of `.claude/worktrees/fnfc013-push`, inside `newgrp uucp`, after the Flipper is rebooted)
```
O=nfc_dumps/pm3_parity/out/2026-10-12-fnfc0111
# step 0 + per-step DWT (probe v3, 5 repeats per length); before = 01.10 firmware, reset = size-only reset with the old FDT
DWT=1 NREP=5 N32=0 N64=0 N80=0 bash $O/tools/measure.sh $O before=727b29c11:v3 reset=3b555a1d7:v3 after=HEAD:v3
python3 nfc_dumps/pm3_parity/out/2026-10-11-fnfc013/tools/dwt_table.py $O before reset after
for L in before reset after; do python3 nfc_dumps/pm3_parity/out/2026-10-11-fnfc013/tools/probe3_table.py $O $L; done
python3 $O/tools/sof_table.py $O "nfc_dumps/SLIX2_2026-02-27_16,08,34.nfc" before reset after
# calibrate: COMP += (316 - median PM3 SOF) * 13.56 if the medians are off by more than ~3 us, rebuild, re-measure
# clean firmware: SOF set x10, 001F x3, 004F x3, reader --all x3, hf 15 dump x3, ISO14443-A regression
DWT=1 NREP=10 N32=3 N64=0 N80=3 NDUMP=3 bash $O/tools/final.sh $O final
python3 $O/tools/sof_table.py $O "nfc_dumps/SLIX2_2026-02-27_16,08,34.nfc" final
# unit tests (local-history image), then flash the clean push-branch firmware back
(cd ../fnfc0110-ut && ./fbt DEBUG=0 FIRMWARE_APP_SET=unit_tests FLIP_PORT=/dev/serial/by-id/usb-Flipper_Devices_Inc._Flipper_E17ana_flip_E17ana-if00 flash_usb_full)
toolchain/current/bin/python3 nfc_dumps/pm3_parity/out/2026-10-11-fnfc013/tools/unit_tests_run.py "unit_tests test_nfc" $O/unit_tests_nfc_run1.log
./fbt DEBUG=0 FLIP_PORT=/dev/serial/by-id/usb-Flipper_Devices_Inc._Flipper_E17ana_flip_E17ana-if00 flash_usb_full
```
Notes: `measure.sh` must measure commits that contain the 01.11 tools directory (it is the output directory), hence
`727b29c11` (firmware-identical to 61a37228e) and not 61a37228e itself. The 01.3 v3 patch no longer applies after
`3b555a1d7`; `measure.sh` falls back to `latency_probe_v3_rebased.patch` (same stamps).

## Observations / open points for review
- **Pre-existing, unchanged:** the single-EOF path (`wait_for_eof`, write/lock with the option flag) sends
  `instance->tx_buffer` after `iso15693_3_listener_run()` has already reset it on that RxEnd, so the delayed reply is an
  empty payload + CRC instead of `00` + CRC. Size-only reset keeps this byte for byte. Worth a separate card.
- The 80-block margin of the model is ~13 µs; a probe build adds a few cycles. If FDT wait at 80 blocks is ≤ 0,
  move the reply CRC out of `send_frame` / shrink the prefill as the card says, before touching the constant.
- The constant is calibrated to the card's PM3 SOF delay (316 µs), which is 2.6 µs below the 4320/fc nominal and well
  inside 4192..4384/fc. Different readers measure SOF differently; t1min (309 µs) keeps ~7 µs margin at the model value.
