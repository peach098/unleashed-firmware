// Host-side check: bulk ISO15693 encoder == old per-bit encoder, index by index.
#include <furi.h>
#include <stm32wbxx_ll_dma.h>
#include <stm32wbxx_ll_tim.h>
DWT_Type host_dwt;
DMA_TypeDef host_dma1;
TIM_TypeDef host_tim2;
#include "digital_signal/digital_signal.c"
#include "digital_signal/digital_sequence.c"
#include "digital_signal/presets/nfc/iso15693_signal.c"

// Reference: the pre-FNFC-01.3 per-bit encoder, verbatim logic
static void ref_encode(DigitalSequence* seq, Iso15693SignalDataRate rate, const uint8_t* d, size_t n) {
    digital_sequence_clear(seq);
    digital_sequence_add_signal(seq, iso15693_get_sequence_index(Iso15693SignalIndexSof, rate));
    for(size_t k = 0; k < n; k++) {
        for(size_t i = 0; i < BITS_IN_BYTE; i++) {
            const uint8_t bit = d[k] & (1U << i);
            digital_sequence_add_signal(
                seq, iso15693_get_sequence_index(bit ? Iso15693SignalIndexOne : Iso15693SignalIndexZero, rate));
        }
    }
    digital_sequence_add_signal(seq, iso15693_get_sequence_index(Iso15693SignalIndexEof, rate));
}

static GPIO_TypeDef port;
static const GpioPin pin = {.port = &port, .pin = 1};
static int frames = 0, fails = 0;

static void check(Iso15693Signal* sig, DigitalSequence* ref, Iso15693SignalDataRate rate, const uint8_t* d, size_t n, const char* name) {
    iso15693_signal_encode(sig, rate, d, n);
    ref_encode(ref, rate, d, n);
    DigitalSequence* s = sig->tx_sequence;
    frames++;
    if(s->size != ref->size || memcmp(s->data, ref->data, ref->size) != 0) {
        fails++;
        printf("MISMATCH %s rate=%d n=%zu size %u vs %u\n", name, rate, n, (unsigned)s->size, (unsigned)ref->size);
    } else if(name) {
        printf("OK %-28s rate=%d bytes=%-4zu indices=%u\n", name, rate, n, (unsigned)s->size);
    }
}

int main(void) {
    Iso15693Signal* sig = iso15693_signal_alloc(&pin);
    DigitalSequence* ref = digital_sequence_alloc(ISO15693_SIGNAL_BUFFER_SIZE, &pin);
#ifdef MUTATE
    sig->bit_tables[1].nibble[9] ^= 0x100;
#endif
    static uint8_t buf[ISO15693_3_LISTENER_BUFFER_SIZE];

    // Named frames: real SLIX2 emulation responses (flags + data + CRC), all-0, all-1, alternating
    const uint8_t rd1[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    const uint8_t inv[] = {0x00, 0x01, 0xAC, 0x83, 0x9A, 0x01, 0x09, 0x01, 0x04, 0xE0, 0x0F, 0x00};
    for(int rate = 0; rate < Iso15693SignalDataRateNum; rate++) {
        check(sig, ref, rate, rd1, sizeof(rd1), "read_single (7B)");
        check(sig, ref, rate, inv, sizeof(inv), "inventory (12B)");
        const size_t lens[] = {0, 1, 35, 131, 259, 323, ISO15693_3_LISTENER_BUFFER_SIZE};
        const char* names[] = {"empty", "1B", "rdm8 (35B)", "rdm32 (131B)", "rdm64 (259B)", "rdm80 (323B)", "max (1027B)"};
        for(size_t l = 0; l < sizeof(lens) / sizeof(lens[0]); l++) {
            for(size_t i = 0; i < lens[l]; i++) buf[i] = (uint8_t)(i * 37 + 11);
            check(sig, ref, rate, buf, lens[l], names[l]);
        }
        memset(buf, 0x00, sizeof(buf)); check(sig, ref, rate, buf, 323, "all 0x00 (323B)");
        memset(buf, 0xFF, sizeof(buf)); check(sig, ref, rate, buf, 323, "all 0xFF (323B)");
        memset(buf, 0xA5, sizeof(buf)); check(sig, ref, rate, buf, 323, "all 0xA5 (323B)");
        uint8_t all[256]; for(int i = 0; i < 256; i++) all[i] = i;
        check(sig, ref, rate, all, 256, "every byte value (256B)");
    }
    // Random frames, all lengths 0..1027, both rates, and re-encoding over a longer previous frame
    srand(1);
    for(int it = 0; it < 4000; it++) {
        size_t n = rand() % (ISO15693_3_LISTENER_BUFFER_SIZE + 1);
        for(size_t i = 0; i < n; i++) buf[i] = rand();
        check(sig, ref, rand() % 2, buf, n, NULL);
    }
    printf("frames compared: %d, mismatches: %d\n", frames, fails);
    iso15693_signal_free(sig);
    digital_sequence_free(ref);
    return fails != 0;
}
