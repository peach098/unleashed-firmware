// ISO15693 listener signal encoder tests (FNFC-01.3).
//
// The encoder and DigitalSequence are firmware-internal (not exported to apps), so the
// sources are compiled into this test plugin to get access to the encoded index sequence.
// Nothing here transmits: only alloc/encode/free run, the DMA/timer code is never reached.
#include <furi.h>
#include <furi_hal.h>

#include "../test.h" // IWYU pragma: keep

#include <digital_signal/digital_signal.c>
#undef TAG
#include <digital_signal/digital_sequence.c>
#undef TAG
#include <digital_signal/presets/nfc/iso15693_signal.c>
#undef TAG

#define TAG "NfcIso15693SignalTest"

#define NFC_ISO15693_SIGNAL_TEST_RANDOM_FRAMES (200U)

// Reference: the per-bit encoder used before FNFC-01.3 (one checked add_signal() per bit)
static void nfc_iso15693_signal_test_reference_encode(
    DigitalSequence* sequence,
    Iso15693SignalDataRate data_rate,
    const uint8_t* data,
    size_t data_size) {
    digital_sequence_clear(sequence);
    digital_sequence_add_signal(
        sequence, iso15693_get_sequence_index(Iso15693SignalIndexSof, data_rate));

    for(size_t i = 0; i < data_size; ++i) {
        for(size_t bit = 0; bit < BITS_IN_BYTE; ++bit) {
            const bool is_one = data[i] & (1U << bit);
            digital_sequence_add_signal(
                sequence,
                iso15693_get_sequence_index(
                    is_one ? Iso15693SignalIndexOne : Iso15693SignalIndexZero, data_rate));
        }
    }

    digital_sequence_add_signal(
        sequence, iso15693_get_sequence_index(Iso15693SignalIndexEof, data_rate));
}

static bool nfc_iso15693_signal_test_compare(
    Iso15693Signal* signal,
    DigitalSequence* reference,
    Iso15693SignalDataRate data_rate,
    const uint8_t* data,
    size_t data_size) {
    iso15693_signal_encode(signal, data_rate, data, data_size);
    nfc_iso15693_signal_test_reference_encode(reference, data_rate, data, data_size);

    const DigitalSequence* encoded = signal->tx_sequence;
    return (encoded->size == reference->size) &&
           (memcmp(encoded->data, reference->data, reference->size) == 0);
}

// Records the first failure instead of returning, see nfc_iso15693_signal_encode_equivalence_test
#define NFC_ISO15693_SIGNAL_TEST_CHECK(test, message) \
    do {                                              \
        if(!failure && !(test)) failure = (message);  \
    } while(0)

void nfc_iso15693_signal_encode_equivalence_test(void) {
    Iso15693Signal* signal = iso15693_signal_alloc(&gpio_ext_pa7);
    DigitalSequence* reference =
        digital_sequence_alloc(ISO15693_SIGNAL_BUFFER_SIZE, &gpio_ext_pa7);
    uint8_t* data = malloc(ISO15693_3_LISTENER_BUFFER_SIZE);
    // mu_assert returns early, so failures are collected and asserted after cleanup
    const char* failure = NULL;

    // Frames the SLIX2 listener actually sends: read single block, inventory, read multiple 8/32/64/80
    const uint8_t read_single[] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x9A, 0x5C};
    const uint8_t inventory[] = {
        0x00, 0x01, 0xAC, 0x83, 0x9A, 0x01, 0x09, 0x01, 0x04, 0xE0, 0x0F, 0x00};
    const size_t read_multi_sizes[] = {35, 131, 259, 323, ISO15693_3_LISTENER_BUFFER_SIZE};

    for(uint32_t rate = 0; rate < Iso15693SignalDataRateNum; ++rate) {
        NFC_ISO15693_SIGNAL_TEST_CHECK(
            nfc_iso15693_signal_test_compare(
                signal, reference, rate, read_single, sizeof(read_single)),
            "read single block frame differs");
        NFC_ISO15693_SIGNAL_TEST_CHECK(
            nfc_iso15693_signal_test_compare(signal, reference, rate, inventory, sizeof(inventory)),
            "inventory frame differs");
        NFC_ISO15693_SIGNAL_TEST_CHECK(
            nfc_iso15693_signal_test_compare(signal, reference, rate, data, 0),
            "empty frame differs");

        for(size_t i = 0; i < COUNT_OF(read_multi_sizes); ++i) {
            for(size_t j = 0; j < read_multi_sizes[i]; ++j) {
                data[j] = (uint8_t)(j * 37U + 11U);
            }
            NFC_ISO15693_SIGNAL_TEST_CHECK(
                nfc_iso15693_signal_test_compare(
                    signal, reference, rate, data, read_multi_sizes[i]),
                "read multiple blocks frame differs");
        }

        for(size_t j = 0; j < 256; ++j) {
            data[j] = (uint8_t)j;
        }
        NFC_ISO15693_SIGNAL_TEST_CHECK(
            nfc_iso15693_signal_test_compare(signal, reference, rate, data, 256),
            "frame with every byte value differs");
    }

    for(uint32_t i = 0; i < NFC_ISO15693_SIGNAL_TEST_RANDOM_FRAMES; ++i) {
        const size_t data_size = furi_hal_random_get() % (ISO15693_3_LISTENER_BUFFER_SIZE + 1);
        furi_hal_random_fill_buf(data, data_size);
        NFC_ISO15693_SIGNAL_TEST_CHECK(
            nfc_iso15693_signal_test_compare(
                signal,
                reference,
                furi_hal_random_get() % Iso15693SignalDataRateNum,
                data,
                data_size),
            "random frame differs");
    }

    // Not an assertion: encode time of an 80-block read response (323 bytes) for the record
    for(size_t j = 0; j < 323; ++j) {
        data[j] = (uint8_t)(j * 37U + 11U);
    }
    FURI_CRITICAL_ENTER();
    uint32_t start = DWT->CYCCNT;
    iso15693_signal_encode(signal, Iso15693SignalDataRateHi, data, 323);
    const uint32_t bulk_cycles = DWT->CYCCNT - start;
    start = DWT->CYCCNT;
    nfc_iso15693_signal_test_reference_encode(reference, Iso15693SignalDataRateHi, data, 323);
    const uint32_t reference_cycles = DWT->CYCCNT - start;
    FURI_CRITICAL_EXIT();
    // printf, not FURI_LOG: the unit test CLI output does not include log lines
    printf(
        "%s: encode 323 B: %lu cycles (%lu us), per-bit reference %lu cycles (%lu us)\r\n",
        TAG,
        bulk_cycles,
        bulk_cycles / 64,
        reference_cycles,
        reference_cycles / 64);

    free(data);
    digital_sequence_free(reference);
    iso15693_signal_free(signal);

    mu_assert(failure == NULL, failure);
}
