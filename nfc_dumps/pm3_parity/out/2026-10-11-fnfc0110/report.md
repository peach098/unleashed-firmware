# FNFC-01.10: exact digital_signal tick arithmetic (2026-10-11/12)

Goal: make the ISO15693 listener's 64- and 80-block replies (259 and 323 B) decode on the Proxmark3: `hf 15 raw -a -c -d 2223AC839A01090104E0004F` should return `(323)` equal to the dump at least 9 times out of 10, and the reply frame should be within ±50 ppm of the real card's.

## Result in short
- All three arithmetic defects are fixed in `lib/digital_signal`, and the device now programs what the host model predicts, to the tick, for every frame. The real 80-block reply was **+1809 ticks (+289 ppm, 28.3 µs too long)** before the fix and is **+0.04 ticks** after it, which is −0.6 ppm against exact multiples of 1/fc. The −0.6 ppm comes from the 10 ps truncation of the ISO15693 constants.
- On clean firmware `cb843e93f`: `004F` → `(323)` = dump, CRC `24 9E` **10/10**; `003F` → `(259)` **9/10**; `001F` **5/5**; `hf 15 dump` **3/3** complete and equal to the dump. Before the fix: `004F` 0/8 and `003F` 0/3.
- `test_nfc` **55/55** twice (54 existing tests plus the new `iso15693_signal_tick_exact`). ISO14443-A: MIFARE Classic emulation reads correctly, `hf 14a info` 3/3, and `hf mf rdbl 0` / `rdsc 0` equal the file.
- DWT EOF→DMA at 80 blocks: **541 µs** (step 3 of 01.3: 542 µs). The prefill is unchanged at 1939 cycles.
- **PM3 reader traces cannot measure a tag frame's length** (see Method). The ppm figures below come from an on-device tick count, which is exact for what the MCU programs. A decode-window sweep was also run. It puts the Flipper's frame, in the PM3's time base, at about **−20 ppm** (model-dependent; 95 % interval −23..−13). The card's frame is 0 ppm by construction, because its subcarrier is derived from the carrier.

## Branches and commits
- `fnfc-01.3-fast-response` (pushed, `402c24f50`): the 6 code commits of 01.3 plus the cherry-picked `875dfbfb0^..afbc42276` (nfc_dumps/ only). The cherry-picks modify tools that were added on the working branch before `875dfbfb0`, so the "deleted by us" conflicts in `tools/{dwt_run,final_checks,measure_steps}.sh` (all under nfc_dumps/) were resolved to the picked version. One more commit, `402c24f50`, brings the 01.3 directory to its `afbc42276` state: 39 files the tools need (host harness, emu/rpc scripts, probe v3) plus `pm3_matrix.py` and the v2 probe patch. The SLIX2 dump `.nfc` is NOT committed; it stays local.
- `fnfc-01.10-tick-arithmetic` (from it, pushed after each step):
  - `446b1646c` nfc_dumps: host test (red on the 01.3 code: 13 failed checks, `host/tick_model_before.txt`)
  - `cb843e93f` digital_signal: exact tick arithmetic, sum-preserving merge, two-sided remainder (+ unit test)
  - `7fb945a8e`, `a5719e949`, and the commit with this report: measurements

## Code change (`lib/digital_signal`, 3 files)
1. **Tick unit.** Internally 5 ps (`DIGITAL_SIGNAL_T_TIM` 3125, which is exactly 15.625 ns). The public `digital_signal_add_period(…, 10 ps)` is unchanged. `digital_signal_ticks_to_reload()` splits the input at 3125 units (exactly 2 ticks), so no intermediate value exceeds 32 bits for *any* `uint32_t` input. Without the split, a plain ×2 would overflow above 21.47 ms, and the API is exported (`+` in api_symbols), so out-of-tree apps may pass up to 42.9 ms. In-tree maximum: ISO15693 low rate 7 551 622 (75.5 µs); ISO14443-A 530 928. `extend_last_period()` now rounds only the added part (the stored period is whole ticks), which also removes the old `reload × T_TIM` multiplication, which could overflow.
2. **Merge.** At a joint where both levels are equal, the carry is `reload_value + 1`. Both values are stored as ARR (ticks − 1), so the merged period now equals the sum of both.
3. **Remainder.** The carry works in both directions with a strict half-tick threshold (T_TIM is odd, so ties are impossible). A tick is removed only if `next->data[0] + carry > 1`; otherwise the correction is deferred to the next joint.
- Hot path: one more compare per signal joint plus one load of `signal_next->data[0]` in the rare negative branch. No tables, copies or allocations. api_symbols.csv is unchanged (88.17). `iso15693_signal.c` and `iso14443_3a_signal.c` are unchanged; no pre-scaling.

## Method
### Why PM3 traces carry no frame-length information (checked in `~/proxmark3/armsrc/iso15693.c`, dc0110c29)
- `GetIso15693AnswerFromTag()` (≈l.1145) computes the tag frame's **Start from End**: `sof_time = eof_time − 32·16 − len·8·8·16 − 32·16` (ssp clocks, ×4 → fc). End−Start in the trace is therefore always `4096 + 4096·len` fc. Checked on every decoded frame, card and Flipper alike, before and after: the difference is 0 (`card_ref_traces.txt`, `*_traces.txt`). The trace stores the duration in units of 32 fc, which is why `pm3delay.py` multiplies `dur` by 32.
- **End is not measured either.** `*eof_time = dma_start_time + samples·16 − 32` is taken when the decoder returns true. The decoder `Handle15693SamplesFromTag()` (high rate, `recv_speed`) syncs once, on the SOF falling edge (`STATE_TAG_SOF_HIGH_END`, `posCount == 13`). After that, `STATE_TAG_RECEIVING_DATA`, `STATE_TAG_EOF` and `STATE_TAG_EOF_TAIL` step through **fixed 8-sample (512 fc) windows with no edge tracking or resync**: `posCount` is reset only on the window count, never on a signal edge. The EOF is thus found a fixed number of samples (64 per byte + EOF) after the SOF detection. End = SOF detection + nominal, quantised to one sample (64 fc = 4.72 µs; the `dma_start_time & 0xfffffff0` gives the same step). The trace's tag timestamps measure only **when the SOF was detected**. The 01.3 claim "no resync" is correct. The tolerance claimed there ("about half a bit") is not: each bit is decided by comparing the sums of samples 1–4 and 5–8 with `threshold_half`, so the window is asymmetric and much narrower (see the decode window below).
- Consequence: methods (a) and (b) of the task, which subtract DWT from PM3 SOF/EOF, cannot see frame length either. The PM3 EOF is derived from the SOF. A table of "ppm from End−Start" would show exactly 0 for every frame and is not given.

### What was used instead
1. **Tick probe (c)** (`tick_probe.patch`, not committed). `digital_sequence_enqueue_period()` adds up `ARR + 1` and counts entries; `furi_hal_nfc_iso15693_listener_tx()` logs `[TICKS] <N>B periods= ticks=` at Info level. This is the exact length of what the MCU programmed (SOF..EOF without the held last level), compared (`tools/ticks_table.py`) with:
   - the host model of the same bytes (`host/run.sh --frame`);
   - the requested durations (10 ps constants);
   - exact multiples of 1/fc.

   The timer's absolute clock (HSE crystal) is a separate question, covered by the next two items.
2. **PM3 decode success** (`hf 15 raw` output, compared byte by byte with the dump). These are the success counts.
3. **Decoder model** (`decoder_sim/`): the PM3 decoder, extracted verbatim (`extract.sh`), fed with an idealised sample stream. One sample per 64 fc; amplitude = 1000 × the modulated fraction of the sample + σ = 20 noise; random phase.
4. **Decode-window sweep** (`bias_probe.patch`, not committed). Every ISO15693 duration is scaled by a known ppm (−45, −20, +120, +145). The tick probe confirms the programmed length, and each point gets 10 × `004F`. The decoder model curve is then fitted with a shift `s` (`tools/bias_table.py`, binomial maximum likelihood).

Tools: `tools/measure.sh` (checkout + probes + flash + verify hash + `series.sh`), `series.sh` (one `hf 15 raw` per trace; **`data clear` before each read**, because `trace clear` clears only the client copy and the device trace silently stops recording at ≈ 31.7 KB, as happened in the `pre`/`post` runs, see Notes), `final.sh` (clean firmware), `iso14a.sh`, `traces_table.py`, `ticks_table.py`, `bias_table.py`, `frames.py`.

## Tables

### Programmed frame length, device tick probe (`ticks_table.md`); device == host model in every row
| reply | before (402c24f50): ticks | before: vs 1/fc | after (cb843e93f): ticks | after: vs 1/fc | ideal (1/fc) ticks |
|---|---|---|---|---|---|
| 7 B (1 blk) | 147 368 | **+243.3 ppm** (+0.56 µs) | 147 332 | −1.0 ppm | 147 332.2 |
| 35 B (8 blk) | 688 783 | +218.6 ppm (+2.35 µs) | 688 632 | −0.7 ppm | 688 632.4 |
| 131 B (32 blk) | 2 545 175 | +257.7 ppm (+10.2 µs) | 2 544 518 | −0.5 ppm | 2 544 519.2 |
| 259 B (64 blk) | 5 020 485 | +288.9 ppm (+22.7 µs) | 5 019 032 | −0.6 ppm | 5 019 034.8 |
| 323 B (80 blk) | 6 258 098 | **+288.6 ppm (+28.2 µs)** | 6 256 289 | **−0.6 ppm (−0.06 µs)** | 6 256 292.6 |

Against the requested durations, the after-fix error is ≤ 0.32 tick for every frame (+0.04 tick for 323 B). The residual −0.6 ppm is `(uint32_t)` truncation of `16e11/fc` (117994 vs 117994.10) and `256e11/fc`. Card: 0 ppm by construction. The ISO15693 tag subcarrier is fc/32, derived from the reader's carrier, so its frame is an exact multiple of 1/fc in the PM3's time base.

### PM3 one-shot reads, `(n)` = dump and CRC ok (from the `hf 15 raw` output; traces in `*_traces.txt`)
| build | 001F (131 B) | 003F (259 B) | 004F (323 B) |
|---|---|---|---|
| before, `pre` (402c24f50 + DWT + tick probe) | 4/5 + 3/3 | 0/3 | **0/5 + 0/3** |
| after, `post` (cb843e93f + DWT + tick probe) | 5/5 + 3/3 | 5/5 + 3/3 | 3/5 + 3/3 (6/8) |
| after, `postdwt` (DWT probe only) | 3/3 | 2/3 | 3/3 |
| **after, `final` clean firmware cb843e93f** | **5/5** | **9/10** | **10/10** (`final_s80_*.compare.txt`: all `OK: (323) flags=00 data_equal=True crc=24 9E`) |
| `hf 15 dump`, clean | | | **3/3** complete, 80/80 equal |

The tick probe logged one frame per request in every series (e.g. `post` 8 × 323 B for 8 requests; 10 per bias point), so every silence is a PM3 decode failure, not a missed transmission.

### Decode window, 80-block reply (`bias_table.md`, `decoder_sim/sim_window.txt`)
| programmed vs 1/fc | decoded | decoder model (no shift) |
|---|---|---|
| −48.5 ppm (bias −45) | 0/10 | 0.43 |
| −23.3 ppm (bias −20) | 4/10 | 0.90 |
| −0.6 ppm (fix, 3 builds) | 19/21 | 1.00 |
| +118.8 ppm (bias +120) | 10/10 | 0.97 |
| +144.1 ppm (bias +145) | 6/10 | 0.46 |
| before the fix: +288.6 ppm | 0/8 | 0.00 |

- The model window at 80 blocks (50 % points) is about −45..+143 ppm, i.e. −4.4..+14 µs, **asymmetric**. The decoder's SOF alignment favours frames that run long.
- On the device, the 50 % points are at about −18 and +148 ppm. A pure shift fits best at `s = −20.5 ppm` (95 % −23..−13). The device window is also somewhat narrower than the model, so `s` depends on the model.
- **Reading:** in the PM3's time base the Flipper's reply is about 20 ppm shorter than its tick count says, which is the HSE (32 MHz, fixed `LL_RCC_HSE_SetCapacitorTuning(0x26)`) vs PM3 crystal offset, plus whatever the model misses. The fixed firmware sits only ≈ 17 ppm above the window's lower edge, which explains 9/10 at 64 blocks and 6/8 on the probe build.
- The real card at 0 ppm sits about as close to that edge. The window is a property of the PM3 decoder and not of the tag, so the Flipper and the card now behave alike up to the crystal offset.

### DWT, probe v2 (`dwt_table.md`, median of n), EOF→DMA µs
| build | 1 blk | 8 | 32 | 64 | 80 | prefill (cycles) |
|---|---|---|---|---|---|---|
| 01.3 step 3 (reference) | 371 | 384 | 438 | 505 | 542 | 1931 |
| `postdwt` (fix, probe v2 only) | 370 | 388 | 435 | 508 | **541** | 1939 |
| `pre` (01.3 + v2 + tick probe) | 375 | 390 | 444 | 512 | 546 | 2358 |
| `post` (fix + v2 + tick probe) | 376 | 397 | 442 | 514 | 547 | 2389 |

The tick probe's two counter updates per ring entry add ≈ 7 µs to the prefill. Like-for-like before/after: 546 vs 547 µs.

### Host model (`host/`)
- `host/run.sh`: the tree's `lib/digital_signal` is compiled unchanged with stubbed DMA that captures every ring entry. Before / after: `tick_model_before.txt` (13 failed checks) / `tick_model.txt` (0 failed).
  - Merge test: 23 → 24 ticks.
  - ISO15693, 2018 frames of 0..1027 B at both rates: worst frame error +21331 → +0.84 ticks; worst edge error ≤ 0.992 tick.
  - ISO14443-A, 2000 frames: −65 → −0.79 ticks.
  - 300 random sequences with mixed joints and small periods: ≤ 1 tick at every edge.
  - Floor test: periods never drop below 2 ticks; deferred corrections.
  - `add_period` over the whole uint32 range: ≤ ½ tick.
- `host/attribution.txt`: each defect on/off. For the **real 80-block reply** against 1/fc:

  | variant | ppm vs 1/fc |
  |---|---|
  | as is | +288.6 |
  | exact unit only | +167.6 |
  | exact + merge fix (one-sided remainder) | +198.6 |
  | exact + two-sided remainder (old merge) | −31.6 |
  | all three | −0.6 |
  | 01.3 `exp_ttim` pre-scaling | **+167.6**, not the −3 ppm the planning model predicted |

  The planning model used a synthetic byte pattern. The real dump is mostly zero bytes, so there are few equal-level joints, and the one-sided remainder dominates. This explains the 01.3 experiment's 2/11 (+16.4 µs, at the upper edge of the window).

### unit_tests
`test_nfc` 55/55, twice (`unit_tests_nfc_run{1,2}.log`), including `iso15693_signal_tick_exact` (new) and `iso15693_signal_encode_equivalence` (unchanged; it compares index sequences, so the new arithmetic does not change its expected values). Encode of 323 B: 8586 cycles, the same as 01.3.

**How the image was built:** `FIRMWARE_APP_SET=unit_tests` on the remote history fails to link with 4 undefined `js_*` symbols. This is pre-existing: on `cb843e93f`, `unit_test_api_table.o` references `js_thread_run`, `js_value_buffer_size`, `js_thread_stop` and `js_value_parse`. As in 01.3 (whose 54/54 ran on local `2415dd016`), the image was built from the local-history branch: a local worktree `.claude/worktrees/fnfc0110-ut`, branch `fnfc0110-ut-local` (not pushed), `afbc42276` + cherry-pick of `cb843e93f` = `f5e110ca6`. `lib/digital_signal` and `nfc_iso15693_signal_test.c` are byte-identical to the push branch; `nfc_test.c` additionally carries the local SLIX2 tests. The device reported `f5e110ca`, not dirty (`unit_tests_run.txt`).

### ISO14443-A regression (clean firmware cb843e93f)
nfc_dumps/ contains no MIFARE Classic dump. The test used `/ext/nfc/MFC_1K_2026-03-23_21,06,03.nfc` from the Flipper SD (UID E3 04 83 29, default keys).
- `hf 14a info`: 3/3, UID E3 04 83 29.
- `hf mf rdbl --blk 0 -k FFFFFFFFFFFF`: `E3 04 83 29 4D 08 04 00 04 57 2D D5 B3 0A A6 90` = file.
- `hf mf rdsc -s 0`: blocks 0–3 = file (`final_14a_info_*.log`, `final_mf_*.log`).

## Acceptance criteria
| criterion | status | evidence / note |
|---|---|---|
| 01.3 branch completed with artifacts and pushed; new commits pushed | **met** | One extra commit `402c24f50` for the tool files the picked commits depend on |
| ppm table before/after from PM3 traces, 32 and 80 blocks, 5 repeats, against card and ideal | **met in substance, by another method** | 5 × 001F and 5 × 004F traces were taken before and after (`pre_s*`, `post_s*`, `final_s*`). They cannot yield ppm (End−Start ≡ nominal, End derived from SOF). The ppm table above comes from the device tick count; the card is 0 ppm by construction |
| Flipper 323 B frame within ±50 ppm of the card | **not directly measurable with the PM3; met by indirect estimate** | Programmed: −0.6 ppm against 1/fc (exact). PM3 time base: ≈ −20 ppm (decode-window fit, model-dependent, 95 % −23..−13) |
| `004F` → `(323)` = dump, CRC `24 9E` ≥ 9/10; `003F` ≥ 9/10 | **met** (clean firmware) | 10/10 and 9/10. The probe build gave 6/8 at 80 blocks; see the decode window |
| host tick model ≤ 1 tick per frame 1–323 B; joint test; equivalence test updated and passing | **met** | `host/tick_model.txt`. The equivalence test needed no change (index sequences) and passes on the device |
| `test_nfc` on device; ISO14443-A emulation read by PM3 3/3 | **met** | 55/55 (the card said 54; +1 new test); `hf 14a info` 3/3 + `rdbl`/`rdsc` |
| EOF→DMA at 80 blocks ≤ 542 ± 10 µs | **met** | 541 µs (probe v2) |
| `./fbt DEBUG=0`, unit_tests build, `./fbt format` clean; `api_symbols.csv` unchanged | **met, except the unit_tests build on the remote history** | DEBUG=0: 0 warnings. Format: only the 3 known subghz files, reverted. unit_tests: links only on the local history (pre-existing `js_*`). api_symbols: unchanged (88.17) |
| `report.md` with tables and trace references | **met** | this file |

## Hypotheses
All three are confirmed on the device code path. The device's tick count equals the host model for every logged frame before the fix (+1809 ticks at 323 B) and after it (+0.04). The per-defect split comes from the host model on the same bytes (`host/attribution.txt`):
- `T_TIM` 1562 instead of 1562.5 accounts for +320 ppm.
- The one-sided remainder adds about +199 ppm once the unit is exact.
- The merge makes each "→1" joint one tick short: −31 ppm on this mostly-zero dump, up to about −207 ppm on dense data.

Residual: −0.6 ppm (constant truncation) plus ≤ 1 tick of arithmetic, plus the crystal offset (≈ −20 ppm in the PM3's time base, estimated).

## Notes for FNFC-01.11 (latency / tx buffer reset)
- EOF→DMA is unchanged by this card: 370/388/435/508/541 µs for 1/8/32/64/80 blocks against 01.3's 371/…/542. The SOF delay seen by the PM3 is 363–373 µs (1 block) and 533–566 µs (80 blocks); the card's is 316 µs.
- The per-joint compare adds no measurable prefill time (1939 vs 1931 cycles).
- The tick probe costs ≈ 7 µs of prefill; do not use it for latency figures.

## Notes for FNFC-01.12 (`hf 15 dump` aborts)
- 3/3 dumps were complete in this session (240 single-block reads, clean firmware). No abort was seen.
- Every silent long reply in this session was transmitted (the tick probe logged it) and lost in the PM3 decoder.
- Suggestion: use the tick probe's per-frame log to tell "not transmitted" from "not decoded" for the short-reply misses.
- Single-block frames (7 B) are only 2 ms long, so the decode window cannot be the cause there.

## Open questions / assumptions
- **Decode-window margin.** The PM3 reader decoder only accepts frames from ≈ −18 to +148 ppm (80 blocks, on this pair of devices). The fixed Flipper is 17–20 ppm above the lower edge, and a unit whose HSE runs about 20 ppm faster would fail the 80-block read regularly. Options, as a human decision and not done here (card requirement 8: no tuning):
  - per-device HSE trim, `HSETUNE`;
  - a deliberate small positive bias of +30..+60 ppm, which would move the frame away from the card's 0 ppm but into the middle of this decoder's window;
  - accepting the margin, because real readers may resync on edges.
- The `s` estimate depends on the idealised decoder model (box-filter amplitude, σ = 20).
- The test count is 55 instead of the 54 in the card, because one test was added.
- The MFC regression used a file from the Flipper SD, not from nfc_dumps/ (there is none). Other ISO14443-A types (NTAG/Ultralight emulation) were exercised only through `test_nfc`, which includes the `iso14443_3a_reader` and `mf_classic_*` tests, not through PM3 emulation reads.

## Risks
- ISO14443-A timing changed too, now exact; before, frames were up to −65 ticks (≈ −1 µs) over 18 B. The checks above pass; other 14443-A cards were not emulated against the PM3.
- The deferral at the 2-tick floor means a pathological sequence, where every signal starts with a 2-tick period and has a negative remainder, never gets its correction and drifts (+6 ticks over 64 signals in `host`). No in-tree user comes near: the shortest periods are 37 ticks (ISO14443-A) and 75 ticks (ISO15693).
- Remainders are now in 5 ps units. `struct DigitalSignal` is private (`digital_signal_i.h`); only the test plugin, which compiles the sources itself, reads it.
- The probe and bias patches are measurement aids. They are kept as patch files in this directory and are not in the code.

## Notes
- In `pre`/`post`/`postdwt`, `series.sh` did not yet use `data clear`. The device trace filled up, and some later `.trace` files in `pre`/`post` are stale (`post_s32/s64/s80`, `post_rdm80_3`). Their success counts are taken from the `hf 15 raw` output (`.log`), and `*_traces.txt` for those series shows "silence" or a wrong request where the trace is stale. `final` and the bias runs use `data clear`.
- Card reference traces (01.1) are copied to `card_ref/`. The `(323)` reply is the response after the last `…004F` request.

## How to reproduce (repo root, inside `newgrp uucp`, SLIX2 dump at `nfc_dumps/SLIX2_2026-02-27_16,08,34.nfc`)
```
bash nfc_dumps/pm3_parity/out/2026-10-11-fnfc0110/host/run.sh                        # host test
python3 nfc_dumps/pm3_parity/out/2026-10-11-fnfc0110/host/attribution.py 402c24f50
O=nfc_dumps/pm3_parity/out/2026-10-11-fnfc0110
DWT=1 N32=5 N64=5 N80=5 bash $O/tools/measure.sh $O pre=402c24f50:dwt,ticks post=HEAD:dwt,ticks
bash $O/tools/final.sh $O final                                                      # clean firmware
DWT=0 N32=0 N80=10 bash $O/tools/measure.sh $O bm20=HEAD:ticks,bias-20 bp145=HEAD:ticks,bias145
python3 $O/tools/ticks_table.py nfc_dumps/SLIX2_2026-02-27_16,08,34.nfc lib $O/post_probe_log.txt
python3 $O/tools/bias_table.py $O bm45:-45 bm20:-20 final:0 post:0 postdwt:0 bp120:120 bp145:145
```
