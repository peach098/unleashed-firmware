| stage | 1 blk (7 B, n=7) | 8 blk (35 B, n=7) | 32 blk (131 B, n=7) | 64 blk (259 B, n=7) | 80 blk (323 B, n=7) |
|---|---|---|---|---|---|
| RxEnd->rx (nfc_listener_rx, parser copy) | 345 (5.4) | 351 (5.5) | 351 (5.5) | 351 (5.5) | 351 (5.5) |
| rx->copy (bit_buffer_copy_bits) | 133 (2.1) | 139 (2.2) | 139 (2.2) | 139 (2.2) | 139 (2.2) |
| copy->run (dispatch to iso15693_3_listener_run) | 90 (1.4) | 90 (1.4) | 90 (1.4) | 90 (1.4) | 90 (1.4) |
| run->crc (request CRC check+trim) | 7908 (123.6) | 7919 (123.7) | 7922 (123.8) | 7922 (123.8) | 7922 (123.8) |
| crc->handler (flags/addr/UID checks) | 7310 (114.2) | 7310 (114.2) | 7310 (114.2) | 7310 (114.2) | 7310 (114.2) |
| handler->send (cmd handler + SLIX ext) | 516 (8.1) | 652 (10.2) | 1228 (19.2) | 1996 (31.2) | 2380 (37.2) |
| send->crc_app (tx copy + reply CRC) | 348 (5.4) | 803 (12.5) | 2357 (36.8) | 4395 (68.7) | 5396 (84.3) |
| crc_app->tx_enter (nfc_listener_tx call) | 74 (1.2) | 74 (1.2) | 74 (1.2) | 74 (1.2) | 74 (1.2) |
| total RxEnd->tx_enter | 16919 (264.4) | 17696 (276.5) | 19663 (307.2) | 22277 (348.1) | 23662 (369.7) |
