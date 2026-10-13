# FNFC-01.13 (F1f): deferred reply after a single EOF (option flag) lost its `00`

Date 2026-10-13. Branch `fnfc-01.13-option-eof` (on `origin/fnfc-01.11-fdt-t1` 99bbb704e).
Code-only branch `fnfc-01-f1f-code` (on `origin/fnfc-01-f1-code`).

## Root cause (confirmed)
A Write/Lock request with `ISO15693_3_REQ_FLAG_T4_OPTION` sets `session_state.wait_for_eof`, and
`handle_standard_request()` leaves the reply (`00`, or `01 0F`) in `tx_buffer` without sending it.
On the next RxEnd (the single EOF, empty rx buffer) `iso15693_3_listener_run()` emptied `tx_buffer`
before it looked at the frame, so `process_single_eof()` sent an empty payload plus CRC: `00 00`.

Two related holes in the same state machine, found while writing the tests:
- `wait_for_eof` was only ever reset by the EOF. After `write -o` with no EOF, the next request's reply
  (e.g. a READ) was suppressed (`if(!wait_for_eof)`) and went out on a later EOF instead.
- A malformed/ignored Write/Lock with the option flag (Format/Ignore = no reply) set `wait_for_eof`, so a
  following EOF would have sent the `00` that `handle_standard_request` puts in front of every reply.

SLIX: same path. SLIX goes through the base ISO15693-3 listener for Write/Lock (the SLIX extension
handlers only veto), and the single EOF is handled in the base `run()`. `slix_listener_i.c` resets its own
tx buffer only in the custom-command path (A0..DF), which never waits for an EOF. So the bug existed
for SLIX emulation too (the PM3 traces below are on SLIX2 emulation); the base fix covers it, nothing
SLIX-specific changed.

## Fix (`lib/nfc/protocols/iso15693_3/`)
- `iso15693_3_listener.c`: the size-only `tx_buffer` reset moved into the valid-CRC branch (right before
  `process_request`); the single-EOF branch now sends what the request left. A `NfcEventTypeFieldOff`
  branch clearing `wait_for_eof` was added as a safeguard only: the F7 ISO15693 HAL
  (`furi_hal_nfc_iso15693_wait_event` returns only RxEnd or Abort) and nfc_mock never report FieldOff,
  so it does not fire today. The reset in the request branch is redundant with the one in
  `handle_standard_request` (other request paths do not send) and is kept as a cheap guard.
- `iso15693_3_listener_i.c`: `process_request()` clears `wait_for_eof` first (any request drops a pending
  deferred reply; Write/Lock with the option flag set it again); Format/Ignore clear it (no reply now, no
  reply after the EOF).
- Conscious choice: a frame with a wrong CRC is not a request and does not drop a pending reply, so
  request-with-option -> corrupted frame -> EOF still sends the deferred reply. Only a valid request
  (or, once the HAL reports it, field off) drops it.
- `process_single_eof()` unchanged: sends only if `wait_for_eof` is set, otherwise
  `Iso15693_3ErrorUnexpectedResponse` -> `Iso15693_3ListenerEventTypeSingleEof` callback, no reply.
- Replies to requests are built exactly as before. `api_symbols.csv` unchanged.

## PM3: `hf 15 wrbl -o` on SLIX2 emulation (card AC dump, block 3 = its current data 02 5A 50 05)
`-u` is LSB-first for PM3: `-u E0040109019A83AC` puts `AC 83 9A 01 09 01 04 E0` on air (the printed UID
AC839A01090104E0 gives `E0 04 ...` on air -> UID mismatch, silence).

| firmware | runs | reply after EOF | PM3 |
|---|---|---|---|
| before, b9c34783e (release 01.12) | 5 (-u) + 1 (-*) | `00 00` (2 B, no valid CRC) | `crc ( fail )`, `Writing ... ( fail )` |
| after, 88414bf8c (release, same lib tree as 8d151c59c) | 10 (-u) + 1 (-*) | `00 78 F0` (3 B, CRC ok) | `Writing ... ( ok )` |
| after, 8d151c59c (final release) | 3 (-u) | `00 78 F0` | `( ok )` |

Without the option flag (before and after): `00 78 F0`, gap 4096-4224 fc, `( ok )` - unchanged.
`hf 15 rdbl -b 3` after the writes: `02 5A 50 05`.
Files: `before_wrbl_opt_*.log`, `after_wrbl_opt_*.log`, `release_wrbl_opt_*.log`, `*_noopt*.log`,
table `wrbl_table.txt` (tool `tools/wrbl_trace.py`).

### Timing of the deferred reply (EOF -> SOF)
PM3 does not record the single EOF (`LogTrace` drops zero-length frames; `trace save -f <relative>` also
resolves against $HOME and failed, so only the `trace list` text was kept). Measurable: end of the write
request -> start of the tag frame = 306944 / 307008 fc (22.64 ms), the same before and after the fix (only
the content changed), jitter one PM3 sample (64 fc). PM3 sends the EOF after its write timeout
(4700 samples x 64 fc = 300800 fc from the first DMA sample) and the EOF takes 1536 fc, so
**EOF -> tag frame <= 4608..4672 fc (339.8..344.5 us)** in `trace list` units; the PM3 code time between
request end, timeout and EOF (tens of us) is not separable. The same `trace list` gap for a normal reply is
4096 fc (302 us): the deferred reply is at most ~40 us later than a normal one, all of which may be PM3
overhead. The reply goes through the same `send_frame` -> `nfc_listener_tx` -> FDT timer path as a normal
reply. An exact number needs a Flipper DWT probe at the EOF (not done).

## No SD write
`/ext/nfc/SLIX2_2026-02-27_16,08,34.nfc` md5 5be0209500cd0fc787c35ce06b0f4834 after the `wrbl -o` runs on
88414bf8c and on 8d151c59c (= the repo dump); no SLIX2 `.shd` appeared (only the two old ISO15693-3 `.shd`
from March). Files: `*_shd_list.txt`, `*_sd_md5.txt`.

## Unit tests (`applications/debug/unit_tests/tests/nfc/nfc_test.c`)
New: `iso15693_3_listener_option_flag_eof_test` (plain ISO15693-3 listener) and
`slix_listener_option_flag_eof_test` (SLIX listener), loopback over nfc_mock; the single EOF is a
zero-length `nfc_poller_trx` frame (no new seam, no API change). Block 60 of Slix_cap_default (above PP 0x32):
- Write Single Block + option: silent; EOF -> exactly `00` (+ CRC checked); second EOF -> silence
- Read (option = security byte) -> `00 00 DE AD BE EF`; EOF after it -> silence (no stale reply)
- Write + option, then Read without option -> answered right away; EOF -> silence
- malformed Write + option -> silence, EOF -> silence
- Lock Block + option: EOF -> `00`; read shows lock byte 01
- errors: write locked block, lock locked block, write block 200, Lock DSFID twice (plus Lock AFI twice
  on plain; Lock AFI without the EAS/AFI password and write-protected block 0 on SLIX): the reply after
  the EOF equals the `01 <code>` the same request gets without the option flag
- Write Multiple Blocks 0..1 + option (plain): EOF -> `00`, block 1 written
- the listener's own image (`nfc_listener_get_data`) ends with block 60 = 11 22 33 44, locked,
  DSFID locked, AFI locked (plain) / not locked (SLIX)

On device (unit_tests image from local history = fnfc0111-ut-local 83b878d86 + this card's 2 commits):

| image | result |
|---|---|
| b88568c24 (tests only, no fix) | FAILED 2: both new tests, step 1 got `00 00` (2 B) - the bug (`ut_red_test_nfc.log`) |
| 16b627860 (tests + fix) | **PASSED, 58 tests (56 + 2)** (`ut_test_nfc.log`) |

Intermediate runs while fixing the test itself: `ut_test_nfc_run1_f048492d4.log` (the listener copies its
data; SLIX Lock AFI needs the password), `ut_test_nfc_diag*.log` (block 40 is below PP 0x32 on SLIX).

## Regression on 88414bf8c (final.sh SKIP_FLASH=1 DWT=1 NREP=10; same lib tree as the final release)
- `hf 15 dump` 3/3: 80/80 equal; `hf 15 reader --all` 3/3 sees the UID (and 1/1 + dump 1/1 on 8d151c59c)
- `hf 15 raw ... 004F`: (323) 9/10 (1 silence, s80_1); 003F (259) 9/10; 001F (131) 5/5
- ISO14443-A: `hf 14a info` 3/3, `hf mf rdbl/rdsc` read
- PM3 SOF (sof_table.py, n=10): 1 block median 316.2 us = 01.11/01.12 (316.2), min 316.2, one sample 382.3;
  8: 320.9, 32: 316.2, 64: 316.2 (9/10), 80: 320.9 (9/10); 80/1 = 1.015.
  The run-path change is one `bit_buffer_set_size(tx, 0)` moved behind the CRC check. No DWT probe built.
Files: `final_*`, `final_sof_table.txt`, `final_stdout.txt`.

## Observations, not changed
- Low data rate: the listener always answers at high data rate (`furi_hal_nfc_iso15693.c`:
  `Iso15693SignalDataRateHi` in tx_prepare / tx / tx_sof), so a deferred reply to a low-rate `write -o`
  would also go out at the wrong rate.
- `iso15693_3_listener_write_multi_blocks_handler` indexes the request data with the absolute block number
  (`block_data[block_size * i]`), wrong for a first block > 0; SLIX rejects Write Multiple anyway. Tests use
  first block 0.
- The base WRITE AFI/DSFID length bug (`data_size <= 1` -> Format) is still on this base (fixed on F2 by
  FNFC-01.15), so Write AFI/DSFID stay silent here with or without the option flag; on F2 they take the
  same deferred path. Tests for them belong on top of F2.
- Real SLIX2 behaviour with the option flag is unknown (no writes to the real card).

## Stacking notes for reviewers
- F3 (FNFC-01.7, 16-slot inventory) also edits `process_request` (clears `inventory_pending`) and adds a
  FieldOff branch in `run()`: textual conflict with this card, trivial (keep both). F3's
  `process_single_eof` rebuilds the inventory reply itself, so it does not depend on the tx reset; its
  comment "The tx buffer has been emptied for this EOF frame" becomes inaccurate (harmless).
- The code-only branch cherry-picks onto `origin/fnfc-01-f1-code` without conflicts (nfc_test.c too).
