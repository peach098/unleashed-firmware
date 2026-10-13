# FNFC-01.12: rare missed short reply in `hf 15 dump` (2026-10-13). Interim, blocked on hardware

**Status:** stopped on a Flipper USB hang (`hw_blocker_evidence.txt`).
- The "before" measurement is complete.
- The probe is written and builds, but it has not run on the device.
- No firmware code was changed.

## Result in short

**The miss reproduces on clean HEAD `e3b768c55` (release build): 3 of 30 `hf 15 dump` runs aborted, after blocks 71, 30 and 23.**

| measurement | missed replies | rate | 95 % confidence interval |
|---|---|---|---|
| this run, `e3b768c55` | 3 in 2287 READBLOCK requests | 1 in 762 | 1/261 .. 1/3695 |
| 01.3, `afbc42276` | 2 in 272 requests | 1 in 136 | 1/38 .. 1/1124 |

- The two intervals overlap, so the miss rate may not have changed since 01.3.
- The 6/6 complete dumps in 01.11 (480 reads) are also consistent with 1 in 762.
- About 10 % of dumps abort. So "10/10 complete" would pass by luck about 35 % of the time.
- A credible fix needs at least 30 clean dumps (2400 reads). At 1 in 762, the chance of zero misses by luck is about 4 %.

**Why one miss kills the whole dump: the PM3 client never retries.**
- In `CmdHF15Dump` (`cmdhf15.c` around line 2452), a successful read sets `retry = 0`.
- The loop's own `retry++` then makes it 1, so after the first block there is no retry.
- A single missed reply prints `iso15693 command failed` and ends the dump.
- This matches the data: each abort log has exactly one `command failed`, and each aborted trace has exactly one request for the failed block.

**What the PM3 trace shows for all 3 misses.**
- The READBLOCK request is in the trace with a valid CRC.
- No tag frame follows. The PM3 logs only frames it decodes, so silence and garbage look the same from its side.

**The PM3 did not send the missed requests early.**
- The pause before each was 1567, 1510 and 1487 µs.
- Over all requests the median pause was 1501 µs, the minimum 429 µs.
- So the plain form of H1 (next request arrives before the receiver is ready) is not supported: the Flipper always had more than 1.4 ms to re-arm.

**First probe data (only 2 records, from probe v1).**
- The signal reader is re-armed 3 µs after the reply ends (`rearm=3`).
- Before each received frame, the signal reader was triggered once by something that was not a SOF (`sofail=1 bad=0A`). The parser restarted and recovered.
- Both records are INVENTORY commands right after the PM3 switched its field on, so this says nothing yet about READBLOCK.
- If such a false trigger lands less than about 75 µs before a request SOF, or the parser restarts late, the SOF is lost. That would produce exactly the trace above.
- Probe v2 measures this directly (`fail2sof`, `maxrestart`).

**A latent firmware bug, found because probe v1 crashed the Flipper (not fixed; separate card proposed).**
- In `iso15693_parser.c`, `signal_reader_callback` (runs in the interrupt handler) appends to the 32-byte `bitstream_buff` without a bounds check, including in the Fail state.
- If the worker thread doesn't run within about 2.4 ms (32 × 75.5 µs), the interrupt handler writes past the buffer into the parser struct (`bytes_to_process`, the `parsed_frame` pointer). The Flipper then crashes.
- Probe v1 logged over USB from the NFC worker thread and hit this twice.
- Release firmware doesn't block the worker that long, so this is probably not the 1-in-762 miss. It is still a memory-safety hole.

## Hypotheses
| | hypothesis | status | evidence so far |
|---|---|---|---|
| H1 | receiver not ready when the request arrives | open; plain form unlikely | PM3 pause 1487–1567 µs before the misses; v1 `rearm=3 µs`. Not closed: a false trigger plus a late restart near the SOF is an H1 variant |
| H2 | request received but dropped (CRC or parser) | open | needs v2 `crc=0`, `pfail>0` or `ovf>0` on a missed block |
| H3 | reply sent but the PM3 didn't decode it | open | needs v2 `tx=1` with normal `eof2dma` on a missed block, then 30 dumps on real card AC (human tag swap) |
| H4 | ST25R3916 transparent mode / GPIO interrupt | not started | |

## Table 1: before (clean `e3b768c55` release, 30 × `hf 15 dump`)
Full per-run table: `pre_table.txt`. Raw files: `pre_dump_*.log/.trace/.compare.txt`.

| run | result | abort after block | missed request / reply | PM3 pause before it |
|---|---|---|---|---|
| 27 runs (1–13, 15–25, 27, 29, 30) | complete, 80/80 equal to the dump | - | - | 429–3766 µs across all runs |
| 14 | ABORTED | 71 | READBLOCK(71), CRC ok / no tag frame | 21248 fc = 1567 µs |
| 26 | ABORTED | 30 | READBLOCK(30), CRC ok / no tag frame | 20480 fc = 1510 µs |
| 28 | ABORTED | 23 | READBLOCK(23), CRC ok / no tag frame | 20160 fc = 1487 µs |

Reply SOF delay over the 2287 answered requests (the PM3 measures in 64-fc steps):

| SOF delay | count | note |
|---|---|---|
| 306.8 µs | 12 | |
| 311.5 µs | 294 | |
| 316.2 µs | 1728 | |
| 320.9 µs | 236 | |
| 325.7 µs | 12 | |
| 335.1 µs | 1 | run 28, block 5 |
| 344.5 µs | 1 | run 14, block 55 |

- Only these two replies were more than one step late, and both are in aborted runs (not on the missed block).
- That is a weak hint of occasional worker-thread latency of +20–30 µs, from only 2 cases.

## Probe (`rx_miss_probe.patch`, not committed into firmware code)
**Build and status.**
- Built with `--extra-define=NFC_RX_MISS_PROBE`: 0 warnings, and the patch applies on clean `e3b768c55`.
- It has not run on the device.

**How v2 logs.**
- It writes one record per received frame and queues it without blocking, right after the signal reader re-arms.
- A Low-priority thread prints it at Info level as a `[P12]` line.
- v1 printed from the worker thread itself and crashed (see above).

**Fields per record.**
- Request: length, command, block number, CRC result.
- Reply: number sent and bytes; `eof2dma` (request end → first reply DMA); `rearm` (reply end → signal reader restart, the H1 window).
- Receiver timing: `txend2arm`, `armed2sof`, `fail2sof` (last false trigger → this SOF).
- Counters since the previous frame: `sofail` (false triggers), `pfail` (bad symbols), `restarts` and `maxrestart` (slowest restart), `ovf` (ISR found its buffer still full), `maxb` (fullest buffer), `wake` (slowest thread wake-up at the 32-symbol mark), `drop` (records lost from the queue).

**Analysis tools in `tools/`.**
- `dump_trace.py`: per-run table from the PM3 traces.
- `p12_table.py`: matches `[P12]` records to dump runs and the missed block, plus field statistics. Written but never run against real records.
- `dumps.sh`, `probe_series.sh`: run the dump series, the latter with log capture.

## Acceptance criteria
| criterion | status |
|---|---|
| 1. Before: runs with traces, plus table | met: 30 runs, 3 aborts |
| 2. Probe on the device, matched to every abort | not met: v2 written and builds; the device hung first |
| 3. Hypothesis decided with evidence | not met: plain H1 argued against by the PM3 pauses; the H1 variant, H2 and H3 are open |
| 4. Fix, 10/10 dumps, EOF→DMA within ±5 µs, `(323)` ≥ 2/3 | N/A: no root cause yet, no code change, so EOF→DMA and `(323)` were not touched |
| 5. Outside the Flipper / not reproducible → document | N/A: it is reproducible (3/30) |
| 6. `test_nfc`, `./fbt format`, push | N/A for code (unchanged); artifacts pushed |

## How to resume (repo root, inside `newgrp uucp`, after the replug)
```
OUT=nfc_dumps/pm3_parity/out/2026-10-13-fnfc0112
git apply $OUT/rx_miss_probe.patch
rm -f build/usbinstall.flag   # otherwise fbt says flash_usb_full "is up to date" and does not flash
./fbt DEBUG=0 --extra-define=NFC_RX_MISS_PROBE FLIP_PORT=/dev/serial/by-id/usb-Flipper_Devices_Inc._Flipper_E17ana_flip_E17ana-if00 flash_usb_full
git apply -R $OUT/rx_miss_probe.patch
bash $OUT/tools/probe_series.sh $OUT probe 40 1      # about 3 misses expected
python3 $OUT/tools/p12_table.py $OUT probe 1 $OUT/probe_probe_log_1.txt
```

How to read the record of each missed block:
- **No received frame for the missed block, and a small `fail2sof` or a non-zero `sofail` in the next record:** a false trigger ate the SOF (H1 variant). Candidate minimal fix: restart the SOF search inside the interrupt handler instead of waiting for the thread to restart the parser.
- **A received frame with `crc=0`, `pfail>0` or `ovf>0`:** H2.
- **A received frame with `crc=1`, `tx=1` and normal `eof2dma`:** H3. Then run 30 dumps on real card AC (human tag swap).

## Risks and open questions
- The probe changes timing a little (DWT reads, a CRC check before the handler, queue writes), even though it no longer blocks on USB. Compare its miss rate with the release build's.
- Unbounded ISR buffer in `signal_reader_callback` deserves its own card: bounds check plus drop-to-Fail.
- For Story FNFC-01: making `hf 15 dump` survive a miss needs a PM3 client retry (out of scope). Practical workaround: repeat the dump.
- Acceptance "10/10 complete dumps" is statistically weak at ~1/762 (≈35 % pass by luck); 30 dumps is the suggested bar.
