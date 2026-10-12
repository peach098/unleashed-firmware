| stage | 1 blk (7 B, n=7) | 8 blk (35 B, n=7) | 32 blk (131 B, n=7) | 64 blk (259 B, n=7) | 80 blk (323 B, n=7) |
|---|---|---|---|---|---|
| RxEnd->rx (nfc_listener_rx, parser copy) | 345 (5.4) | 351 (5.5) | 351 (5.5) | 351 (5.5) | 351 (5.5) |
| rx->copy (bit_buffer_copy_bits) | 133 (2.1) | 139 (2.2) | 139 (2.2) | 139 (2.2) | 139 (2.2) |
| copy->run (dispatch to iso15693_3_listener_run) | 90 (1.4) | 90 (1.4) | 90 (1.4) | 90 (1.4) | 90 (1.4) |
| run->crc (request CRC check+trim) | 506 (7.9) | 517 (8.1) | 520 (8.1) | 520 (8.1) | 520 (8.1) |
| crc->handler (flags/addr/UID checks) | 324 (5.1) | 324 (5.1) | 324 (5.1) | 324 (5.1) | 324 (5.1) |
| handler->send (cmd handler + SLIX ext) | 514 (8.0) | 645 (10.1) | 1221 (19.1) | 1989 (31.1) | 2373 (37.1) |
| send->crc_app (tx copy + reply CRC) | 348 (5.4) | 803 (12.5) | 2354 (36.8) | 4392 (68.6) | 5393 (84.3) |
| crc_app->tx_enter (nfc_listener_tx call) | 74 (1.2) | 74 (1.2) | 74 (1.2) | 74 (1.2) | 74 (1.2) |
| total RxEnd->tx_enter | 2334 (36.5) | 2943 (46.0) | 5073 (79.3) | 7879 (123.1) | 9264 (144.8) |
