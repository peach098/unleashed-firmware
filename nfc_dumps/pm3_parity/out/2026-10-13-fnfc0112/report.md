# FNFC-01.12: rare missed short reply in `hf 15 dump` — root cause and fix (2026-10-13, final)

Supersedes the interim report.md (6b731e4ab). Plain text because subagents cannot write .md report files.

## Result in short
- Root cause is on the Flipper side: H2, a request that was received but dropped by the ISO15693 parser
  (lib/signal_reader/parsers/iso15693/iso15693_parser.c). There are two independent mechanisms.
  - A (handoff race). The ISR -> thread hand-off of the 32-symbol buffer lost a symbol when the worker
    thread had not consumed the chunk before the 33rd symbol arrived. The frame then failed its CRC and was
    dropped silently.
  - B (missed EOF; dominant in release, no CLI session attached). The 8 samples of the EOF slot held no
    pause (00 instead of 04). The parser stored that as a data symbol, failed the frame as "invalid
    symbol", and gave no reply.
- The fix is 3 commits in iso15693_parser.c; nothing else changed.
- 0 misses in 366 `hf 15 dump` runs on the fixed code (about 29 000 single-block reads), in 3 host
  environments. Before: 29 aborts in 230 dumps without a CLI session.
- The PM3 `hf 15 dump` client has no retry after block 0. One missed reply aborts the dump, which turns a
  1-in-500 reply loss into a 10-15 % dump failure rate.
- Latency:
  - 1-block PM3 SOF median 316.2 us, same as 01.11. Release mean over 4800 replies: 316.51 vs 316.06 us
    before (+0.45 us).
  - DWT EOF->DMA, 1 block: 317 us on b81a96abf vs 313 us for 01.11 (+4 us, within the +-5 us limit;
    efbbf22af then removed about 1.5 us more, by PM3 mean).
  - (323) = dump 10/10.
- Not done: `test_nfc` on the device, and the DWT run on the very last commit. The Flipper USB hung again
  at 14:53 with a probe build (see Hardware state).

## Environment effect (new, important for every PM3 timing measurement)
An attached CLI `log` session on the Flipper changes the results.

| build | environment | dumps | aborted | SOF mean (us) |
|---|---|---|---|---|
| e3b768c55 | CLI `log info` attached (pre2log) | 100 | 0 | 311.4 |
| e3b768c55 | no session (pre2q, emu_open_quiet.py) | 100 | 16 | 316.1 |
| e3b768c55 | RPC session left open by emu_open.py (pre2) | 100 | 10 | 316.1 |
| e3b768c55 | RPC session left open (pre) | 30 | 3 | 316.1 |

- This explains the 01.11 "probe build lands 4.7 us earlier than the release build": all 01.11 probe
  measurements had `log` attached, and the release measurements (final.sh) did not.
- Mechanism B needs the no-session condition.
- Tools: `tools/emu_open_quiet.py` (no screenshot; RPC stop_session), `tools/log_series.sh`.

## Table 1: before (e3b768c55 release, traces)
Per-run table: `pre_table.txt` (30 runs).

| set | dumps | aborts | missed blocks |
|---|---|---|---|
| pre | 30 | 3 | 71, 30, 23 |
| pre2 | 100 | 10 | |
| pre2q | 100 | 16 | |
| pre2log | 100 | 0 | |

- All aborts show the same trace: the READBLOCK request is present with a good CRC and no tag frame
  follows.
- PM3 pause before the missed request: 1487-1567 us (pre). That is normal: all requests have median
  1501 us, minimum 429 us.

## Evidence chain (probe = rx_miss_probe*.patch, records [P12], one per RxEnd)
1. Mechanism A, probe v2/v3 on the old parser, `log` attached:
   - probe: 1/48 dumps aborted; probe3: 4/45.
   - Every miss: the Flipper got an RxEnd of len=12 crc=0 (no reply), with ovf=1 maxb=33.
   - In the 4 probe3 misses: ovfdt = 74-75 us (the 33rd symbol arrived while the chunk was still
     unconsumed), done = 76-77 us.
   - Thread "chunk full -> consumed" over 3418 records: median 49 us, tail to 84 us, against a symbol
     period of 75.5 us; 31 records were >= 75 us.
2. Fix A (3cd724652), probe4 (log attached): 0/150 dumps (12 000 reads).
   - 101 of 4000 records had maxb=33 (thread late, symbol kept), each a would-be miss under the old code.
3. But release builds of fix A without a session: post 17/145, same as the control. A second mechanism
   was present.
4. Probe v5-v7 on fix A without any session (records held until `log debug` is attached afterwards):
   misses reproduced (16/100, 13/60, 11/60).
   - v6: in 13/13 misses the last READBLOCK the Flipper answered was block N-1 (crc=1, tx=1, eof2dma
     319-321 us, rearm 3 us).
   - The window that contains the missed request has sofs=2, pfail=1, restarts=3, against sofs=1,
     pfail=0, restarts=2 in all 46 complete runs. So the SOF of the missed request was seen and the
     frame died on an invalid symbol.
   - v7 pfinfo: 12/12 failed frames have PP=0x34 (symbol 52 = the EOF slot after all 13 bytes), bad
     symbol 00, previous symbol a valid data symbol (02/08/20/80), nothing after it.
     - 11 of them are the 11 aborts.
     - The 12th was READBLOCK(0), which the PM3 retried (the only retry `hf 15 dump` has).
     - So the EOF pause was not sampled at all.
   - ISR event gaps stayed nominal (gapmax 76-79 us), so the ISR was not delayed; the sampled signal
     itself had no pause in that slot.
5. Fix B (b81a96abf) plus the latency tweak efbbf22af, all with 0 misses:

   | commit | environment | dumps |
   |---|---|---|
   | b81a96abf | no session | 100 |
   | b81a96abf | RPC open | 50 |
   | b81a96abf | log attached | 50 |
   | b81a96abf | final.sh | 3 |
   | efbbf22af | no session | 160 |
   | efbbf22af | final.sh | 3 |

   - efbbf22af alone: 13 000 reads; P(0 misses | old rate 1/457) is about e^-28.

## Hypotheses closed
| | status | evidence |
|---|---|---|
| H1 receiver not ready | closed: no | rearm 3-10 us after the reply end; armed2sof median 1.6 ms; the missed request's SOF was detected (sofs=2) |
| H2 received but dropped | confirmed, 2 mechanisms | A: crc=0 len=12, ovf=1, ovfdt 74-75, done 76-77 us. B: pfail at symbol 52 = EOF slot, 00 instead of 04 |
| H3 reply sent, PM3 did not decode it | closed: no | no reply was ever sent for the missed block (the last record with tx=1 is N-1). No real-card swap needed |
| H4 ST25R3916 / GPIO ISR | not needed | ISR event gaps nominal; why the EOF pause is not sampled (PM3 EOF shape vs Flipper sample phase) stays open, and the fix makes it harmless |

## Fix (lib/signal_reader/parsers/iso15693/iso15693_parser.c only; +65/-25)
1. 3cd724652, lossless ISR -> thread hand-off.
   - Buffer of 64 symbols, still woken every 32.
   - The thread consumes exactly the symbols it parsed (snapshot plus memmove of the rest, in a short
     critical section).
   - EOF is sampled before the count.
   - A frame that ends on a chunk boundary completes on EOF.
   - The ISR drops the frame (Fail) instead of writing past the buffer. This also closes the memory-safety
     hole found by the probe v1 crash.
2. b81a96abf, end a 1-of-4 frame on a pause-less symbol.
   - A pause-less symbol (00) right after a whole byte (symbols % 4 == 0) ends the frame; the listener
     still checks the CRC. Timing is that of a decoded EOF (same sample window).
   - Once EOF (or a single EOF) has been seen, the ISR ignores further events (idle-carrier 00 symbols
     were appended as data, or turned SOF state into Fail, when the thread was late).
3. efbbf22af: the hand-back is skipped when the frame is complete or failed, which keeps the critical
   section off the EOF -> reply path. Release SOF mean went 318.0 -> 316.5 us.

## Acceptance criteria
| criterion | status |
|---|---|
| 1. before table with traces | met (330 dumps, 4 sets) |
| 2. probe on device, matched with every abort | met (v3: 4/4; v6: 13/13; v7: 12/12) |
| 3. hypothesis with evidence | met (H2, two mechanisms; H1/H3 closed) |
| 4a. >= 10/10 (bar 30) complete = dump | met: 160/160 (efbbf22af, no session) + 3/3 final.sh; b81a96abf 203/203 |
| 4b. EOF->DMA 1 block 313 +-5 us (DWT) | met on b81a96abf: 317 us. efbbf22af only by PM3: 1-block median 316.2 = 01.11, mean +0.45 us vs before. DWT on efbbf22af was cut off by the USB hang |
| 4c. (323) >= 2/3 | met: 10/10; 001F 3/3; reader --all 3/3; 14a info 3/3; rdbl/rdsc equal |
| 6a. test_nfc | NOT run: device hung. The ut image (fnfc0110-ut, 2146a3877 = be0ea1c4a + the 3 fixes) and fap_test_nfc build. Note: test_nfc does not exercise the parser (frames are injected after it) |
| 6b. ./fbt format | clean (3 subghz files reverted) |
| 6c. pushed | yes (artifact branch + code-only fnfc-01-f1e-code) |

PM3 SOF, final.sh on efbbf22af, n=10 per length:

| blocks | 1 | 8 | 32 | 64 | 80 |
|---|---|---|---|---|---|
| median (us) | 316.2 | 318.6 | 318.6 | 316.2 | 320.9 |

min 311.5 us (>= 309), 80/1 = 1.015.

## Risks / review points
- The pause-less-symbol EOF rule. A real 1-of-4 frame cannot contain a 00 symbol: every symbol has
  exactly one pause. Any frame that ends this way still goes through the CRC check. In 1-of-256 mode
  the rule does not apply.
- The critical section in iso15693_parser_consume() runs in the thread per 32 symbols while a frame
  is open: memmove of 0-2 bytes, taskENTER_CRITICAL.
- `bytes_to_process` and `eof_received` are now volatile.
- The root of B (why the EOF pause is not sampled, and why it depends on the USB/CLI session) is not
  explained. It might affect other readers too; the fix tolerates it.

## Hardware state at the end
- Flipper: USB CDC hung again (write timeout) at ~14:53. It was running probe v8 on efbbf22af (dirty).
  The hang came right after the first frame with `log info` attached. The same probe on b81a96abf ran
  fine an hour earlier, and release efbbf22af ran 163 dumps without trouble.
- Needs a replug. Then:
  - flash the clean release efbbf22af: `rm build/usbinstall.flag` first;
  - flash the ut image from .claude/worktrees/fnfc0110-ut (2146a3877) and run `unit_tests test_nfc`;
  - optionally redo the DWT 1-block run with rx_miss_probe_v8_final.patch (series.sh LOG=1 DWT=1 NREP=10).
- NFC app: was open (emulation) when it hung.
- PM3: fine.

## Files
- Tables: pre_table.txt.
- Raw data: pre*/post*/probe*/final* compare.txt + traces; dump logs in *_dump_logs.tar.xz.
- Probe records: probe*_probe_log*.txt, dwt_b81a96abf/.
- Probe patches: rx_miss_probe.patch (v2), _v3, _v4_on_fix, _v5_on_fix, _v6_on_fix, _v7_on_fix, _v8_final.
- tools/: dumps.sh, probe_series.sh, log_series.sh, emu_open_quiet.py, logcap_debug.py, dump_trace.py,
  p12_table.py.
- hw_blocker_evidence.txt.
