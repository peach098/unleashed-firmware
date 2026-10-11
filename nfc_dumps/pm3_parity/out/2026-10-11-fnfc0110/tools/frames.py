"""ISO15693 helpers shared by the FNFC-01.10 analysis scripts."""
import re

FC = 13.56e6
TIM = 64e6
SUB_10PS = int(16.0e11 / FC)         # (uint32_t)ISO15693_SIGNAL_FC_16, as requested by iso15693_signal.c
SIL_10PS = int(256.0e11 / FC)        # (uint32_t)(ISO15693_SIGNAL_FC_256 * 1)
BIT_10PS = SIL_10PS + 16 * SUB_10PS


def crc15693(data: bytes) -> bytes:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0x8408 if crc & 1 else crc >> 1
    crc = ~crc & 0xFFFF
    return bytes([crc & 0xFF, crc >> 8])


def dump_data(nfc_path: str) -> bytes:
    return bytes.fromhex(re.search(r"^Data Content:(.*)$", open(nfc_path).read(), re.M).group(1))


def read_reply(data: bytes, first: int, count: int) -> bytes:
    """ReadSingle/ReadMultiple reply without option flag: 00 + blocks + CRC."""
    body = bytes([0]) + data[first * 4:(first + count) * 4]
    return body + crc15693(body)


def ideal_ticks(nbytes: int, physical: bool = False) -> float:
    """Ticks the listener should put into the DMA ring for an n-byte high-rate frame: SOF + bits + EOF minus
    the final held half subcarrier period. physical=False: the durations iso15693_signal.c requests
    (10 ps truncated constants); physical=True: exact multiples of 1/fc."""
    if physical:
        fc_units = 2560 + 512 * 8 * nbytes - 16
        return fc_units / FC * TIM
    total = 2 * (48 * SUB_10PS + BIT_10PS) + 8 * nbytes * BIT_10PS - SUB_10PS
    return total / 1562.5


def pm3_nominal_fc(nbytes: int) -> int:
    """End - Start that the PM3 reader writes for a tag frame (armsrc/iso15693.c GetIso15693AnswerFromTag):
    Start is computed from End, so this is constant per length: SOF 32*16*4 + bytes 8*8*16*4 + EOF 32*16*4 fc."""
    return 32 * 16 * 4 + nbytes * 8 * 8 * 16 * 4 + 32 * 16 * 4
