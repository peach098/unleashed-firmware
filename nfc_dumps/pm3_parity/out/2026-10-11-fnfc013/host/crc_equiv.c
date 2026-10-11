#include <furi.h>
#include "toolbox/bit_buffer.c"
#include "nfc/helpers/iso13239_crc.c"

// Reference: the pre-FNFC-01.3 bitwise implementation
static uint16_t ref_crc(Iso13239CrcType type, const uint8_t* data, size_t n) {
    uint16_t crc = type == Iso13239CrcTypeDefault ? 0xFFFF : 0xE012;
    for(size_t i = 0; i < n; ++i) {
        crc ^= (uint16_t)data[i];
        for(size_t j = 0; j < 8; ++j) crc = (crc & 1U) ? (crc >> 1) ^ 0x8408 : crc >> 1;
    }
    return type == Iso13239CrcTypePicopass ? crc : ~crc;
}

int main(void) {
    BitBuffer* b = bit_buffer_alloc(2048);
    static uint8_t d[2048];
    int n_ok = 0, n_bad = 0;
    srand(7);
    for(int it = 0; it < 20000; it++) {
        size_t n = it < 1100 ? (size_t)it : (size_t)(rand() % 1100);
        for(size_t i = 0; i < n; i++) d[i] = rand();
        Iso13239CrcType t = (it & 1) ? Iso13239CrcTypePicopass : Iso13239CrcTypeDefault;
        bit_buffer_copy_bytes(b, d, n);
        iso13239_crc_append(t, b);
        uint16_t got; memcpy(&got, bit_buffer_get_data(b) + n, 2);
        uint16_t exp = ref_crc(t, d, n);
        bool chk = n > 0 ? iso13239_crc_check(t, b) : true;
        if(got == exp && chk && bit_buffer_get_size_bytes(b) == n + 2) n_ok++; else { n_bad++; if(n_bad < 5) printf("BAD n=%zu t=%d got %04x exp %04x\n", n, t, got, exp); }
    }
    // Known vector: ISO15693 inventory response of card AC -> CRC bytes 0F 00? (frame from PM3 trace)
    const uint8_t inv[] = {0x00, 0x01, 0xAC, 0x83, 0x9A, 0x01, 0x09, 0x01, 0x04, 0xE0};
    bit_buffer_copy_bytes(b, inv, sizeof(inv)); iso13239_crc_append(Iso13239CrcTypeDefault, b);
    printf("inventory CRC bytes: %02X %02X\n", bit_buffer_get_data(b)[10], bit_buffer_get_data(b)[11]);
    printf("crc frames ok=%d bad=%d\n", n_ok, n_bad);
    return n_bad != 0;
}
