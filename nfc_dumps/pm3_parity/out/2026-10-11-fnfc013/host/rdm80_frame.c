// Host check: the 80-block ReadMultipleBlocks response built from the dump with the table CRC
// equals the real card's PM3-captured response (00 + 320 data bytes + CRC 24 9E).
#include <furi.h>
#include "toolbox/bit_buffer.c"
#include "nfc/helpers/iso13239_crc.c"
int main(int argc, char** argv) {
    // argv[1]: "Data Content" hex of the .nfc dump, argv[2]: PM3 "(323)" response hex
    BitBuffer* b = bit_buffer_alloc(1027);
    bit_buffer_append_byte(b, 0x00);
    for(char* p = argv[1]; *p; ) { unsigned v; int n; if(sscanf(p, "%x%n", &v, &n) != 1) break; bit_buffer_append_byte(b, v); p += n; }
    iso13239_crc_append(Iso13239CrcTypeDefault, b);
    size_t i = 0; int bad = 0;
    for(char* p = argv[2]; *p; i++) { unsigned v; int n; if(sscanf(p, "%x%n", &v, &n) != 1) break; if(i >= bit_buffer_get_size_bytes(b) || bit_buffer_get_byte(b, i) != v) bad++; p += n; }
    printf("built %zu bytes, card %zu bytes, CRC %02X %02X, mismatches %d\n", bit_buffer_get_size_bytes(b), i,
           bit_buffer_get_byte(b, bit_buffer_get_size_bytes(b) - 2), bit_buffer_get_byte(b, bit_buffer_get_size_bytes(b) - 1), bad);
    return bad || i != bit_buffer_get_size_bytes(b);
}
