#include <furi.h>
#include <furi_hal.h>
#include <storage/storage.h>
#include <flipper_format/flipper_format.h>

#include <nfc/nfc_device.h>
#include <nfc/helpers/nfc_data_generator.h>
#include <nfc/nfc_poller.h>
#include <nfc/nfc_listener.h>
#include <nfc/protocols/iso14443_3a/iso14443_3a.h>
#include <nfc/protocols/iso14443_3a/iso14443_3a_poller.h>
#include <nfc/protocols/iso14443_3a/iso14443_3a_poller_sync.h>
#include <nfc/protocols/mf_ultralight/mf_ultralight.h>
#include <nfc/protocols/mf_ultralight/mf_ultralight_poller_sync.h>
#include <nfc/protocols/mf_classic/mf_classic_poller_sync.h>
#include <nfc/protocols/felica/felica.h>
#include <nfc/protocols/felica/felica_poller_sync.h>
#include <nfc/protocols/mf_classic/mf_classic_poller.h>
#include <nfc/protocols/iso15693_3/iso15693_3_poller.h>
#include <nfc/protocols/slix/slix.h>
#include <nfc/protocols/slix/slix_i.h>
#include <nfc/protocols/slix/slix_poller.h>
#include <nfc/protocols/slix/slix_poller_i.h>
#include <nfc/protocols/mf_plus/mf_plus_crypto.h>
#include <nfc/helpers/iso13239_crc.h>

#include <nfc/nfc_poller.h>

#include <toolbox/keys_dict.h>
#include <nfc/nfc.h>

#include "../test.h" // IWYU pragma: keep

#define TAG "NfcTest"

#define NFC_TEST_NFC_DEV_PATH                  EXT_PATH("unit_tests/nfc/nfc_device_test.nfc")
#define NFC_APP_MF_CLASSIC_DICT_UNIT_TEST_PATH EXT_PATH("unit_tests/mf_dict.nfc")

#define NFC_TEST_FLAG_WORKER_DONE (1)

#define ISO15693_3_LISTENER_TEST_RX_SIZE (64U)
#define ISO15693_3_LISTENER_TEST_CRC_MAX (1027U)

// nfc_iso15693_signal_test.c
void nfc_iso15693_signal_encode_equivalence_test(void);
void nfc_iso15693_signal_tick_exact_test(void);

typedef enum {
    NfcTestMfClassicSendFrameTestStateAuth,
    NfcTestMfClassicSendFrameTestStateReadBlock,

    NfcTestMfClassicSendFrameTestStateFail,
    NfcTestMfClassicSendFrameTestStateSuccess,
} NfcTestMfClassicSendFrameTestState;

typedef struct {
    NfcTestMfClassicSendFrameTestState state;
    BitBuffer* tx_buf;
    BitBuffer* rx_buf;
    FuriThreadId thread_id;
} NfcTestMfClassicSendFrameTest;

typedef enum {
    NfcTestSlixPollerSetPasswordStateGetRandomNumber,
    NfcTestSlixPollerSetPasswordStateSetPassword,
} NfcTestSlixPollerSetPasswordState;

typedef struct {
    FuriThreadId thread_id;
    NfcTestSlixPollerSetPasswordState state;
    SlixRandomNumber random_number;
    SlixPassword password;
    SlixError error;
} NfcTestSlixPollerSetPasswordContext;

typedef struct {
    Storage* storage;
} NfcTest;

static NfcTest* nfc_test = NULL;

static void nfc_test_alloc(void) {
    nfc_test = malloc(sizeof(NfcTest));
    nfc_test->storage = furi_record_open(RECORD_STORAGE);
}

static void nfc_test_free(void) {
    furi_check(nfc_test);

    furi_record_close(RECORD_STORAGE);
    free(nfc_test);
    nfc_test = NULL;
}

static void nfc_test_save_and_load(NfcDevice* nfc_device_ref) {
    NfcDevice* nfc_device_dut = nfc_device_alloc();

    mu_assert(
        nfc_device_save(nfc_device_ref, NFC_TEST_NFC_DEV_PATH), "nfc_device_save() failed\r\n");

    mu_assert(
        nfc_device_load(nfc_device_dut, NFC_TEST_NFC_DEV_PATH), "nfc_device_load() failed\r\n");

    mu_assert(
        nfc_device_is_equal(nfc_device_ref, nfc_device_dut),
        "nfc_device_data_dut != nfc_device_data_ref\r\n");

    mu_assert(
        storage_simply_remove(nfc_test->storage, NFC_TEST_NFC_DEV_PATH),
        "storage_simply_remove() failed\r\n");

    nfc_device_free(nfc_device_dut);
}

static void iso14443_3a_file_test(uint8_t uid_len) {
    NfcDevice* nfc_device = nfc_device_alloc();

    Iso14443_3aData* data = iso14443_3a_alloc();
    data->uid_len = uid_len;
    furi_hal_random_fill_buf(data->uid, uid_len);
    furi_hal_random_fill_buf(data->atqa, sizeof(data->atqa));
    furi_hal_random_fill_buf(&data->sak, 1);

    nfc_device_set_data(nfc_device, NfcProtocolIso14443_3a, data);
    nfc_test_save_and_load(nfc_device);

    iso14443_3a_free(data);
    nfc_device_free(nfc_device);
}

static void nfc_file_test_with_generator(NfcDataGeneratorType type) {
    NfcDevice* nfc_device_ref = nfc_device_alloc();

    nfc_data_generator_fill_data(type, nfc_device_ref);
    nfc_test_save_and_load(nfc_device_ref);

    nfc_device_free(nfc_device_ref);
}

MU_TEST(iso14443_3a_4b_file_test) {
    iso14443_3a_file_test(4);
}

MU_TEST(iso14443_3a_7b_file_test) {
    iso14443_3a_file_test(7);
}

MU_TEST(mf_ultralight_file_test) {
    nfc_file_test_with_generator(NfcDataGeneratorTypeMfUltralight);
}

MU_TEST(mf_ultralight_ev1_11_file_test) {
    nfc_file_test_with_generator(NfcDataGeneratorTypeMfUltralightEV1_11);
}

MU_TEST(mf_ultralight_ev1_h11_file_test) {
    nfc_file_test_with_generator(NfcDataGeneratorTypeMfUltralightEV1_H11);
}

MU_TEST(mf_ultralight_ev1_21_file_test) {
    nfc_file_test_with_generator(NfcDataGeneratorTypeMfUltralightEV1_21);
}

MU_TEST(mf_ultralight_ev1_h21_file_test) {
    nfc_file_test_with_generator(NfcDataGeneratorTypeMfUltralightEV1_H21);
}

MU_TEST(mf_ultralight_ntag_203_file_test) {
    nfc_file_test_with_generator(NfcDataGeneratorTypeNTAG203);
}

MU_TEST(mf_ultralight_ntag_213_file_test) {
    nfc_file_test_with_generator(NfcDataGeneratorTypeNTAG213);
}

MU_TEST(mf_ultralight_ntag_215_file_test) {
    nfc_file_test_with_generator(NfcDataGeneratorTypeNTAG215);
}

MU_TEST(mf_ultralight_ntag_216_file_test) {
    nfc_file_test_with_generator(NfcDataGeneratorTypeNTAG216);
}

MU_TEST(mf_ultralight_ntag_i2c_1k_file_test) {
    nfc_file_test_with_generator(NfcDataGeneratorTypeNTAGI2C1k);
}

MU_TEST(mf_ultralight_ntag_i2c_2k_file_test) {
    nfc_file_test_with_generator(NfcDataGeneratorTypeNTAGI2C2k);
}

MU_TEST(mf_ultralight_ntag_i2c_plus_1k_file_test) {
    nfc_file_test_with_generator(NfcDataGeneratorTypeNTAGI2CPlus1k);
}

MU_TEST(mf_ultralight_ntag_i2c_plus_2k_file_test) {
    nfc_file_test_with_generator(NfcDataGeneratorTypeNTAGI2CPlus2k);
}

MU_TEST(mf_classic_mini_file_test) {
    nfc_file_test_with_generator(NfcDataGeneratorTypeMfClassicMini);
}

MU_TEST(mf_classic_1k_4b_file_test) {
    nfc_file_test_with_generator(NfcDataGeneratorTypeMfClassic1k_4b);
}

MU_TEST(mf_classic_1k_7b_file_test) {
    nfc_file_test_with_generator(NfcDataGeneratorTypeMfClassic1k_7b);
}

MU_TEST(mf_classic_4k_4b_file_test) {
    nfc_file_test_with_generator(NfcDataGeneratorTypeMfClassic4k_4b);
}

MU_TEST(mf_classic_4k_7b_file_test) {
    nfc_file_test_with_generator(NfcDataGeneratorTypeMfClassic4k_7b);
}

MU_TEST(iso14443_3a_reader) {
    Nfc* poller = nfc_alloc();
    Nfc* listener = nfc_alloc();

    Iso14443_3aData iso14443_3a_listener_data = {
        .uid_len = 7,
        .uid = {0x04, 0x51, 0x5C, 0xFA, 0x6F, 0x73, 0x81},
        .atqa = {0x44, 0x00},
        .sak = 0x00,
    };
    NfcListener* iso3_listener =
        nfc_listener_alloc(listener, NfcProtocolIso14443_3a, &iso14443_3a_listener_data);
    nfc_listener_start(iso3_listener, NULL, NULL);

    Iso14443_3aData iso14443_3a_poller_data = {};
    mu_assert(
        iso14443_3a_poller_sync_read(poller, &iso14443_3a_poller_data) == Iso14443_3aErrorNone,
        "iso14443_3a_poller_sync_read() failed");

    nfc_listener_stop(iso3_listener);
    mu_assert(
        iso14443_3a_is_equal(&iso14443_3a_poller_data, &iso14443_3a_listener_data),
        "Data not matches");

    nfc_listener_free(iso3_listener);
    nfc_free(listener);
    nfc_free(poller);
}

static void mf_ultralight_reader_test(const char* path) {
    FURI_LOG_I(TAG, "Testing file: %s", path);
    Nfc* poller = nfc_alloc();
    Nfc* listener = nfc_alloc();

    NfcDevice* nfc_device = nfc_device_alloc();
    mu_assert(nfc_device_load(nfc_device, path), "nfc_device_load() failed\r\n");

    MfUltralightData* data =
        (MfUltralightData*)nfc_device_get_data(nfc_device, NfcProtocolMfUltralight);

    uint32_t features = mf_ultralight_get_feature_support_set(data->type);
    bool pwd_supported =
        mf_ultralight_support_feature(features, MfUltralightFeatureSupportPasswordAuth);
    uint8_t pwd_num = mf_ultralight_get_pwd_page_num(data->type);
    const uint8_t zero_pwd[4] = {0, 0, 0, 0};

    if(pwd_supported && !memcmp(data->page[pwd_num].data, zero_pwd, sizeof(zero_pwd))) {
        data->pages_read -= 2;
    }

    NfcListener* mfu_listener = nfc_listener_alloc(listener, NfcProtocolMfUltralight, data);

    nfc_listener_start(mfu_listener, NULL, NULL);

    MfUltralightData* mfu_data = mf_ultralight_alloc();
    MfUltralightError error = mf_ultralight_poller_sync_read_card(poller, mfu_data, NULL);
    mu_assert(error == MfUltralightErrorNone, "mf_ultralight_poller_sync_read_card() failed");

    nfc_listener_stop(mfu_listener);
    nfc_listener_free(mfu_listener);

    MfUltralightData* mfu_other_data =
        (MfUltralightData*)nfc_device_get_data(nfc_device, NfcProtocolMfUltralight);
    mu_assert(mf_ultralight_is_equal(mfu_data, mfu_other_data), "Data mismatch");

    mf_ultralight_free(mfu_data);
    nfc_device_free(nfc_device);
    nfc_free(listener);
    nfc_free(poller);
}

MU_TEST(mf_ultralight_11_reader) {
    mf_ultralight_reader_test(EXT_PATH("unit_tests/nfc/Ultralight_11.nfc"));
}

MU_TEST(mf_ultralight_21_reader) {
    mf_ultralight_reader_test(EXT_PATH("unit_tests/nfc/Ultralight_21.nfc"));
}

MU_TEST(mf_ultralight_c_reader) {
    mf_ultralight_reader_test(EXT_PATH("unit_tests/nfc/Ultralight_C.nfc"));
}

MU_TEST(ntag_215_reader) {
    mf_ultralight_reader_test(EXT_PATH("unit_tests/nfc/Ntag215.nfc"));
}

MU_TEST(ntag_216_reader) {
    mf_ultralight_reader_test(EXT_PATH("unit_tests/nfc/Ntag216.nfc"));
}

MU_TEST(ntag_213_locked_reader) {
    FURI_LOG_I(TAG, "Testing Ntag215 locked file");
    Nfc* poller = nfc_alloc();
    Nfc* listener = nfc_alloc();

    NfcDeviceData* nfc_device = nfc_device_alloc();
    mu_assert(
        nfc_device_load(nfc_device, EXT_PATH("unit_tests/nfc/Ntag213_locked.nfc")),
        "nfc_device_load() failed\r\n");

    NfcListener* mfu_listener = nfc_listener_alloc(
        listener,
        NfcProtocolMfUltralight,
        nfc_device_get_data(nfc_device, NfcProtocolMfUltralight));
    nfc_listener_start(mfu_listener, NULL, NULL);

    MfUltralightData* mfu_data = mf_ultralight_alloc();
    MfUltralightError error = mf_ultralight_poller_sync_read_card(poller, mfu_data, NULL);
    mu_assert(error == MfUltralightErrorNone, "mf_ultralight_poller_sync_read_card() failed");

    nfc_listener_stop(mfu_listener);
    nfc_listener_free(mfu_listener);

    MfUltralightConfigPages* config = NULL;
    const MfUltralightData* mfu_ref_data =
        nfc_device_get_data(nfc_device, NfcProtocolMfUltralight);
    mu_assert(
        mf_ultralight_get_config_page(mfu_ref_data, &config),
        "mf_ultralight_get_config_page() failed");
    uint16_t pages_locked = config->auth0;

    mu_assert(mfu_data->pages_read == pages_locked, "Unexpected pages read");

    mf_ultralight_free(mfu_data);
    nfc_device_free(nfc_device);
    nfc_free(listener);
    nfc_free(poller);
}

static void mf_ultralight_write(void) {
    Nfc* poller = nfc_alloc();
    Nfc* listener = nfc_alloc();

    NfcDevice* nfc_device = nfc_device_alloc();
    nfc_data_generator_fill_data(NfcDataGeneratorTypeMfUltralightEV1_21, nfc_device);

    NfcListener* mfu_listener = nfc_listener_alloc(
        listener,
        NfcProtocolMfUltralight,
        nfc_device_get_data(nfc_device, NfcProtocolMfUltralight));
    nfc_listener_start(mfu_listener, NULL, NULL);

    MfUltralightData* mfu_data = mf_ultralight_alloc();

    // Initial read
    MfUltralightError error = mf_ultralight_poller_sync_read_card(poller, mfu_data, NULL);
    mu_assert(error == MfUltralightErrorNone, "mf_ultralight_poller_sync_read_card() failed");

    mu_assert(
        mf_ultralight_is_equal(mfu_data, nfc_device_get_data(nfc_device, NfcProtocolMfUltralight)),
        "Data not matches");

    // Write random data
    for(size_t i = 5; i < 15; i++) {
        MfUltralightPage page = {};
        FURI_LOG_D(TAG, "Writing page %d", i);
        furi_hal_random_fill_buf(page.data, sizeof(MfUltralightPage));
        mfu_data->page[i] = page;
        error = mf_ultralight_poller_sync_write_page(poller, i, &page);
        mu_assert(error == MfUltralightErrorNone, "mf_ultralight_poller_sync_write_page() failed");
    }

    // Verification read
    error = mf_ultralight_poller_sync_read_card(poller, mfu_data, NULL);
    mu_assert(error == MfUltralightErrorNone, "mf_ultralight_poller_sync_read_card() failed");

    nfc_listener_stop(mfu_listener);
    const MfUltralightData* mfu_listener_data =
        nfc_listener_get_data(mfu_listener, NfcProtocolMfUltralight);

    mu_assert(mf_ultralight_is_equal(mfu_data, mfu_listener_data), "Data not matches");

    nfc_listener_free(mfu_listener);
    mf_ultralight_free(mfu_data);
    nfc_device_free(nfc_device);
    nfc_free(listener);
    nfc_free(poller);
}

static void mf_classic_reader(void) {
    Nfc* poller = nfc_alloc();
    Nfc* listener = nfc_alloc();

    NfcDevice* nfc_device = nfc_device_alloc();
    nfc_data_generator_fill_data(NfcDataGeneratorTypeMfClassic4k_7b, nfc_device);
    NfcListener* mfc_listener = nfc_listener_alloc(
        listener, NfcProtocolMfClassic, nfc_device_get_data(nfc_device, NfcProtocolMfClassic));
    nfc_listener_start(mfc_listener, NULL, NULL);

    MfClassicBlock block = {};
    MfClassicKey key = {.data = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};

    mf_classic_poller_sync_read_block(poller, 0, &key, MfClassicKeyTypeA, &block);

    nfc_listener_stop(mfc_listener);
    nfc_listener_free(mfc_listener);

    const MfClassicData* mfc_data = nfc_device_get_data(nfc_device, NfcProtocolMfClassic);
    mu_assert(memcmp(&mfc_data->block[0], &block, sizeof(MfClassicBlock)) == 0, "Data mismatch");

    nfc_device_free(nfc_device);
    nfc_free(listener);
    nfc_free(poller);
}

static void mf_classic_write(void) {
    Nfc* poller = nfc_alloc();
    Nfc* listener = nfc_alloc();

    NfcDevice* nfc_device = nfc_device_alloc();
    nfc_data_generator_fill_data(NfcDataGeneratorTypeMfClassic4k_7b, nfc_device);
    NfcListener* mfc_listener = nfc_listener_alloc(
        listener, NfcProtocolMfClassic, nfc_device_get_data(nfc_device, NfcProtocolMfClassic));
    nfc_listener_start(mfc_listener, NULL, NULL);

    MfClassicBlock block_write = {};
    MfClassicBlock block_read = {};
    furi_hal_random_fill_buf(block_write.data, sizeof(MfClassicBlock));
    MfClassicKey key = {.data = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};

    mf_classic_poller_sync_write_block(poller, 1, &key, MfClassicKeyTypeA, &block_write);
    mf_classic_poller_sync_read_block(poller, 1, &key, MfClassicKeyTypeA, &block_read);

    nfc_listener_stop(mfc_listener);
    nfc_listener_free(mfc_listener);

    mu_assert(memcmp(&block_read, &block_write, sizeof(MfClassicBlock)) == 0, "Data mismatch");

    nfc_device_free(nfc_device);
    nfc_free(listener);
    nfc_free(poller);
}

static void mf_classic_value_block(void) {
    Nfc* poller = nfc_alloc();
    Nfc* listener = nfc_alloc();

    NfcDevice* nfc_device = nfc_device_alloc();
    nfc_data_generator_fill_data(NfcDataGeneratorTypeMfClassic4k_7b, nfc_device);
    NfcListener* mfc_listener = nfc_listener_alloc(
        listener, NfcProtocolMfClassic, nfc_device_get_data(nfc_device, NfcProtocolMfClassic));
    nfc_listener_start(mfc_listener, NULL, NULL);

    MfClassicKey key = {.data = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};

    int32_t value = 228;
    MfClassicBlock block_write = {};
    mf_classic_value_to_block(value, 1, &block_write);

    MfClassicError error = MfClassicErrorNone;
    error = mf_classic_poller_sync_write_block(poller, 1, &key, MfClassicKeyTypeA, &block_write);
    mu_assert(error == MfClassicErrorNone, "Write failed");

    int32_t data = 200;
    int32_t new_value = 0;
    error =
        mf_classic_poller_sync_change_value(poller, 1, &key, MfClassicKeyTypeA, data, &new_value);
    mu_assert(error == MfClassicErrorNone, "Value increment failed");
    mu_assert(new_value == value + data, "Value not match");

    error =
        mf_classic_poller_sync_change_value(poller, 1, &key, MfClassicKeyTypeA, -data, &new_value);
    mu_assert(error == MfClassicErrorNone, "Value decrement failed");
    mu_assert(new_value == value, "Value not match");

    nfc_listener_stop(mfc_listener);
    nfc_listener_free(mfc_listener);
    nfc_device_free(nfc_device);
    nfc_free(listener);
    nfc_free(poller);
}

NfcCommand mf_classic_poller_send_frame_callback(NfcGenericEventEx event, void* context) {
    furi_check(event.poller);
    furi_check(event.parent_event_data);
    furi_check(context);

    NfcCommand command = NfcCommandContinue;
    MfClassicPoller* instance = event.poller;
    NfcTestMfClassicSendFrameTest* frame_test = context;
    Iso14443_3aPollerEvent* iso3_event = event.parent_event_data;

    MfClassicError error = MfClassicErrorNone;
    if(iso3_event->type == Iso14443_3aPollerEventTypeReady) {
        if(frame_test->state == NfcTestMfClassicSendFrameTestStateAuth) {
            MfClassicKey key = {
                .data = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
            };
            error = mf_classic_poller_auth(instance, 0, &key, MfClassicKeyTypeA, NULL, false);
            frame_test->state = (error == MfClassicErrorNone) ?
                                    NfcTestMfClassicSendFrameTestStateReadBlock :
                                    NfcTestMfClassicSendFrameTestStateFail;
        } else if(frame_test->state == NfcTestMfClassicSendFrameTestStateReadBlock) {
            do {
                const uint8_t read_block_cmd[] = {
                    0x30,
                    0x01,
                    0x8b,
                    0xb9,
                };
                bit_buffer_copy_bytes(frame_test->tx_buf, read_block_cmd, sizeof(read_block_cmd));

                error = mf_classic_poller_send_encrypted_frame(
                    instance, frame_test->tx_buf, frame_test->rx_buf, 200000);
                if(error != MfClassicErrorNone) break;
                if(bit_buffer_get_size_bytes(frame_test->rx_buf) != 18) {
                    error = MfClassicErrorProtocol;
                    break;
                }

                const uint8_t* rx_data = bit_buffer_get_data(frame_test->rx_buf);
                const uint8_t rx_data_ref[16] = {0};
                if(memcmp(rx_data, rx_data_ref, sizeof(rx_data_ref)) != 0) {
                    error = MfClassicErrorProtocol;
                    break;
                }
            } while(false);

            frame_test->state = (error == MfClassicErrorNone) ?
                                    NfcTestMfClassicSendFrameTestStateSuccess :
                                    NfcTestMfClassicSendFrameTestStateFail;
        } else if(frame_test->state == NfcTestMfClassicSendFrameTestStateSuccess) {
            command = NfcCommandStop;
        } else if(frame_test->state == NfcTestMfClassicSendFrameTestStateFail) {
            command = NfcCommandStop;
        }
    } else {
        frame_test->state = NfcTestMfClassicSendFrameTestStateFail;
        command = NfcCommandStop;
    }

    if(command == NfcCommandStop) {
        furi_thread_flags_set(frame_test->thread_id, NFC_TEST_FLAG_WORKER_DONE);
    }

    return command;
}

MU_TEST(mf_classic_send_frame_test) {
    Nfc* poller = nfc_alloc();
    Nfc* listener = nfc_alloc();

    NfcDevice* nfc_device = nfc_device_alloc();
    nfc_data_generator_fill_data(NfcDataGeneratorTypeMfClassic4k_7b, nfc_device);
    NfcListener* mfc_listener = nfc_listener_alloc(
        listener, NfcProtocolMfClassic, nfc_device_get_data(nfc_device, NfcProtocolMfClassic));
    nfc_listener_start(mfc_listener, NULL, NULL);

    NfcPoller* mfc_poller = nfc_poller_alloc(poller, NfcProtocolMfClassic);
    NfcTestMfClassicSendFrameTest context = {
        .state = NfcTestMfClassicSendFrameTestStateAuth,
        .thread_id = furi_thread_get_current_id(),
        .tx_buf = bit_buffer_alloc(32),
        .rx_buf = bit_buffer_alloc(32),
    };
    nfc_poller_start_ex(mfc_poller, mf_classic_poller_send_frame_callback, &context);

    uint32_t flag =
        furi_thread_flags_wait(NFC_TEST_FLAG_WORKER_DONE, FuriFlagWaitAny, FuriWaitForever);
    mu_assert(flag == NFC_TEST_FLAG_WORKER_DONE, "Wrong thread flag");
    nfc_poller_stop(mfc_poller);
    nfc_poller_free(mfc_poller);

    mu_assert(
        context.state == NfcTestMfClassicSendFrameTestStateSuccess, "Wrong test state at the end");

    bit_buffer_free(context.tx_buf);
    bit_buffer_free(context.rx_buf);
    nfc_listener_stop(mfc_listener);
    nfc_listener_free(mfc_listener);
    nfc_device_free(nfc_device);
    nfc_free(listener);
    nfc_free(poller);
}

MU_TEST(mf_classic_dict_test) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(storage_common_stat(storage, NFC_APP_MF_CLASSIC_DICT_UNIT_TEST_PATH, NULL) == FSE_OK) {
        mu_assert(
            storage_simply_remove(storage, NFC_APP_MF_CLASSIC_DICT_UNIT_TEST_PATH),
            "Remove test dict failed");
    }

    KeysDict* dict = keys_dict_alloc(
        NFC_APP_MF_CLASSIC_DICT_UNIT_TEST_PATH, KeysDictModeOpenAlways, sizeof(MfClassicKey));
    mu_assert(dict != NULL, "keys_dict_alloc() failed");

    size_t dict_keys_total = keys_dict_get_total_keys(dict);
    mu_assert(dict_keys_total == 0, "keys_dict_keys_total() failed");

    const uint32_t test_key_num = 30;
    MfClassicKey* key_arr_ref = malloc(test_key_num * sizeof(MfClassicKey));
    for(size_t i = 0; i < test_key_num; i++) {
        furi_hal_random_fill_buf(key_arr_ref[i].data, sizeof(MfClassicKey));
        mu_assert(
            keys_dict_add_key(dict, key_arr_ref[i].data, sizeof(MfClassicKey)), "add key failed");

        size_t dict_keys_total = keys_dict_get_total_keys(dict);
        mu_assert(dict_keys_total == (i + 1), "keys_dict_keys_total() failed");
    }

    keys_dict_free(dict);

    dict = keys_dict_alloc(
        NFC_APP_MF_CLASSIC_DICT_UNIT_TEST_PATH, KeysDictModeOpenAlways, sizeof(MfClassicKey));
    mu_assert(dict != NULL, "keys_dict_alloc() failed");

    dict_keys_total = keys_dict_get_total_keys(dict);
    mu_assert(dict_keys_total == test_key_num, "keys_dict_keys_total() failed");

    MfClassicKey key_dut = {};
    size_t key_idx = 0;
    while(keys_dict_get_next_key(dict, key_dut.data, sizeof(MfClassicKey))) {
        mu_assert(
            memcmp(key_arr_ref[key_idx].data, key_dut.data, sizeof(MfClassicKey)) == 0,
            "Loaded key data mismatch");
        key_idx++;
    }

    uint32_t delete_keys_idx[] = {1, 3, 9, 11, 19, 27};

    for(size_t i = 0; i < COUNT_OF(delete_keys_idx); i++) {
        MfClassicKey* key = &key_arr_ref[delete_keys_idx[i]];
        mu_assert(
            keys_dict_is_key_present(dict, key->data, sizeof(MfClassicKey)),
            "keys_dict_is_key_present() failed");
        mu_assert(
            keys_dict_delete_key(dict, key->data, sizeof(MfClassicKey)),
            "keys_dict_delete_key() failed");
    }

    dict_keys_total = keys_dict_get_total_keys(dict);
    mu_assert(
        dict_keys_total == test_key_num - COUNT_OF(delete_keys_idx),
        "keys_dict_keys_total() failed");

    keys_dict_free(dict);
    free(key_arr_ref);

    mu_assert(
        storage_simply_remove(storage, NFC_APP_MF_CLASSIC_DICT_UNIT_TEST_PATH),
        "Remove test dict failed");
}

static FelicaError
    felica_do_request_response(FelicaData* felica_data, const FelicaCardKey* card_key) {
    NfcDeviceData* nfc_device = nfc_device_alloc();

    FelicaError error = FelicaErrorNone;
    if(!nfc_device_load(nfc_device, EXT_PATH("unit_tests/nfc/Felica.nfc"))) {
        error = FelicaErrorNotPresent;
    } else {
        Nfc* poller = nfc_alloc();
        Nfc* listener = nfc_alloc();
        NfcListener* felica_listener = nfc_listener_alloc(
            listener, NfcProtocolFelica, nfc_device_get_data(nfc_device, NfcProtocolFelica));
        nfc_listener_start(felica_listener, NULL, NULL);

        error = felica_poller_sync_read(poller, felica_data, card_key);

        nfc_listener_stop(felica_listener);
        nfc_listener_free(felica_listener);

        nfc_free(listener);
        nfc_free(poller);
    }

    nfc_device_free(nfc_device);
    return error;
}

MU_TEST(felica_read) {
    FelicaData* felica_data = felica_alloc();
    FelicaError error = felica_do_request_response(felica_data, NULL);
    mu_assert(error == FelicaErrorNone, "felica_poller() failed");
    mu_assert(felica_data->data.fs.spad[4].SF1 == 0x01, "block[4].SF1 != 0x01");
    mu_assert(felica_data->data.fs.spad[4].SF2 == 0xB1, "block[4].SF2 != 0xB1");

    felica_free(felica_data);
}

// Minimal check of the FeliCa file format v3: no radio involved, just parsing
MU_TEST(felica_standard_file_test) {
    NfcDevice* nfc_device = nfc_device_alloc();
    mu_assert(
        nfc_device_load(nfc_device, EXT_PATH("unit_tests/nfc/Felica_Standard_with_keys.nfc")),
        "nfc_device_load() failed");

    const FelicaData* data = nfc_device_get_data(nfc_device, NfcProtocolFelica);
    mu_assert(data->workflow_type == FelicaStandard, "workflow_type != FelicaStandard");
    mu_assert(simple_array_get_count(data->systems) == 1, "system count != 1");

    const FelicaSystem* system = simple_array_cget(data->systems, 0);
    mu_assert(system->system_code == 0x0003, "system_code != 0x0003");
    mu_assert(system->key_version == 0x0001, "system key_version != 0x0001");

    mu_assert(simple_array_get_count(system->areas) == 1, "area count != 1");
    const FelicaArea* area = simple_array_cget(system->areas, 0);
    mu_assert(area->code == 0x0000, "area code != 0x0000");
    mu_assert(area->end_code == 0xFFFE, "area end_code != 0xFFFE");
    mu_assert(area->key_version == 0x0002, "area key_version != 0x0002");

    mu_assert(simple_array_get_count(system->services) == 2, "service count != 2");
    const FelicaService* service0 = simple_array_cget(system->services, 0);
    mu_assert(service0->code == 0x0009, "service[0] code != 0x0009");
    mu_assert(service0->key_version == 0x0003, "service[0] key_version != 0x0003");
    const FelicaService* service1 = simple_array_cget(system->services, 1);
    mu_assert(service1->code == 0x000B, "service[1] code != 0x000B");
    mu_assert(service1->key_version == 0x0004, "service[1] key_version != 0x0004");

    nfc_device_free(nfc_device);
}

// Version 2 files carry neither area end codes nor key versions
MU_TEST(felica_standard_v2_file_test) {
    NfcDevice* nfc_device = nfc_device_alloc();
    mu_assert(
        nfc_device_load(nfc_device, EXT_PATH("unit_tests/nfc/Felica_Standard_v2.nfc")),
        "nfc_device_load() failed");

    const FelicaData* data = nfc_device_get_data(nfc_device, NfcProtocolFelica);
    mu_assert(data->workflow_type == FelicaStandard, "workflow_type != FelicaStandard");
    mu_assert(simple_array_get_count(data->systems) == 1, "system count != 1");

    const FelicaSystem* system = simple_array_cget(data->systems, 0);
    mu_assert(system->system_code == 0x0003, "system_code != 0x0003");
    mu_assert(system->key_version == FELICA_KEY_VERSION_UNKNOWN, "system key_version is known");

    mu_assert(simple_array_get_count(system->areas) == 1, "area count != 1");
    const FelicaArea* area = simple_array_cget(system->areas, 0);
    mu_assert(area->code == 0x0000, "area code != 0x0000");
    mu_assert(area->end_code == FELICA_AREA_END_CODE_UNKNOWN, "area end_code is known");
    mu_assert(area->key_version == FELICA_KEY_VERSION_UNKNOWN, "area key_version is known");
    mu_assert(area->first_idx == 0, "area first_idx != 0");
    mu_assert(area->last_idx == 1, "area last_idx != 1");

    mu_assert(simple_array_get_count(system->services) == 2, "service count != 2");
    const FelicaService* service0 = simple_array_cget(system->services, 0);
    mu_assert(service0->code == 0x0009, "service[0] code != 0x0009");
    mu_assert(service0->key_version == FELICA_KEY_VERSION_UNKNOWN, "service[0] key_version known");
    const FelicaService* service1 = simple_array_cget(system->services, 1);
    mu_assert(service1->code == 0x000B, "service[1] code != 0x000B");
    mu_assert(service1->key_version == FELICA_KEY_VERSION_UNKNOWN, "service[1] key_version known");

    mu_assert(simple_array_get_count(system->public_blocks) == 2, "public block count != 2");

    nfc_device_free(nfc_device);
}

// Version 3 files stored the root area key version in the system field, so it is not trusted
MU_TEST(felica_standard_v3_file_test) {
    NfcDevice* nfc_device = nfc_device_alloc();
    mu_assert(
        nfc_device_load(nfc_device, EXT_PATH("unit_tests/nfc/Felica_Standard_v3.nfc")),
        "nfc_device_load() failed");

    const FelicaData* data = nfc_device_get_data(nfc_device, NfcProtocolFelica);
    mu_assert(data->workflow_type == FelicaStandard, "workflow_type != FelicaStandard");
    mu_assert(simple_array_get_count(data->systems) == 1, "system count != 1");

    const FelicaSystem* system = simple_array_cget(data->systems, 0);
    mu_assert(system->system_code == 0x0003, "system_code != 0x0003");
    mu_assert(system->key_version == FELICA_KEY_VERSION_UNKNOWN, "system key_version is known");

    mu_assert(simple_array_get_count(system->areas) == 1, "area count != 1");
    const FelicaArea* area = simple_array_cget(system->areas, 0);
    mu_assert(area->code == 0x0000, "area code != 0x0000");
    mu_assert(area->end_code == 0xFFFE, "area end_code != 0xFFFE");
    mu_assert(area->key_version == 0x0002, "area key_version != 0x0002");

    mu_assert(simple_array_get_count(system->services) == 2, "service count != 2");
    const FelicaService* service0 = simple_array_cget(system->services, 0);
    mu_assert(service0->key_version == 0x0003, "service[0] key_version != 0x0003");
    const FelicaService* service1 = simple_array_cget(system->services, 1);
    mu_assert(service1->key_version == 0x0004, "service[1] key_version != 0x0004");

    nfc_device_free(nfc_device);
}

MU_TEST(felica_oversized_lite_file_test) {
    NfcDevice* nfc_device = nfc_device_alloc();
    mu_assert(
        !nfc_device_load(nfc_device, EXT_PATH("unit_tests/nfc/Felica_oversized_lite.nfc")),
        "oversized FeliCa Lite file was accepted");
    nfc_device_free(nfc_device);
}

MU_TEST(felica_standard_read) {
    NfcDeviceData* nfc_device = nfc_device_alloc();
    mu_assert(
        nfc_device_load(nfc_device, EXT_PATH("unit_tests/nfc/Felica_Standard_with_keys.nfc")),
        "nfc_device_load() failed");

    Nfc* poller = nfc_alloc();
    Nfc* listener = nfc_alloc();
    NfcListener* felica_listener = nfc_listener_alloc(
        listener, NfcProtocolFelica, nfc_device_get_data(nfc_device, NfcProtocolFelica));
    nfc_listener_start(felica_listener, NULL, NULL);

    FelicaData* felica_data = felica_alloc();
    FelicaError error = felica_poller_sync_read(poller, felica_data, NULL);

    nfc_listener_stop(felica_listener);
    nfc_listener_free(felica_listener);
    nfc_free(listener);
    nfc_free(poller);
    nfc_device_free(nfc_device);

    mu_assert(error == FelicaErrorNone, "felica_poller_sync_read() failed");
    mu_assert(simple_array_get_count(felica_data->systems) == 1, "system count != 1");

    const FelicaSystem* system = simple_array_cget(felica_data->systems, 0);
    mu_assert(system->key_version == 0x0001, "system key_version != 0x0001");

    mu_assert(simple_array_get_count(system->areas) == 1, "area count != 1");
    const FelicaArea* area = simple_array_cget(system->areas, 0);
    mu_assert(area->code == 0x0000, "area code != 0x0000");
    mu_assert(area->key_version == 0x0002, "area key_version != 0x0002");

    mu_assert(simple_array_get_count(system->services) == 2, "service count != 2");
    const FelicaService* service0 = simple_array_cget(system->services, 0);
    mu_assert(service0->code == 0x0009, "service[0] code != 0x0009");
    mu_assert(service0->key_version == 0x0003, "service[0] key_version != 0x0003");
    const FelicaService* service1 = simple_array_cget(system->services, 1);
    mu_assert(service1->code == 0x000B, "service[1] code != 0x000B");
    mu_assert(service1->key_version == 0x0004, "service[1] key_version != 0x0004");

    mu_assert(simple_array_get_count(system->public_blocks) == 2, "public block count != 2");

    const FelicaPublicBlock* pb0 = simple_array_cget(system->public_blocks, 0);
    mu_assert(pb0->service_code == 0x0009, "block[0] service_code != 0x0009");
    mu_assert(pb0->block_idx == 0, "block[0] block_idx != 0");
    static const uint8_t expected_block0[FELICA_DATA_BLOCK_SIZE] = {
        0x01,
        0x02,
        0x03,
        0x04,
        0x05,
        0x06,
        0x07,
        0x08,
        0x09,
        0x0A,
        0x0B,
        0x0C,
        0x0D,
        0x0E,
        0x0F,
        0x10};
    mu_assert(
        memcmp(pb0->block.data, expected_block0, FELICA_DATA_BLOCK_SIZE) == 0,
        "block[0] data mismatch");

    const FelicaPublicBlock* pb1 = simple_array_cget(system->public_blocks, 1);
    mu_assert(pb1->service_code == 0x000B, "block[1] service_code != 0x000B");
    mu_assert(pb1->block_idx == 0, "block[1] block_idx != 0");
    static const uint8_t expected_block1[FELICA_DATA_BLOCK_SIZE] = {
        0xA1,
        0xA2,
        0xA3,
        0xA4,
        0xA5,
        0xA6,
        0xA7,
        0xA8,
        0xA9,
        0xAA,
        0xAB,
        0xAC,
        0xAD,
        0xAE,
        0xAF,
        0xB0};
    mu_assert(
        memcmp(pb1->block.data, expected_block1, FELICA_DATA_BLOCK_SIZE) == 0,
        "block[1] data mismatch");

    felica_free(felica_data);
}

MU_TEST(felica_read_auth) {
    FelicaData* felica_data = felica_alloc();
    FelicaCardKey card_key;
    memset(card_key.data, 0xFF, FELICA_DATA_BLOCK_SIZE);

    FelicaError error = felica_do_request_response(felica_data, &card_key);
    mu_assert(error == FelicaErrorNone, "felica_poller() failed");
    mu_assert(felica_data->data.fs.spad[4].SF1 == 0x00, "block[4].SF1 != 0x00");
    mu_assert(felica_data->data.fs.spad[4].SF2 == 0x00, "block[4].SF2 != 0x00");

    felica_free(felica_data);
}

MU_TEST(slix_file_with_capabilities_test) {
    NfcDevice* nfc_device_missed_cap = nfc_device_alloc();
    mu_assert(
        nfc_device_load(nfc_device_missed_cap, EXT_PATH("unit_tests/nfc/Slix_cap_missed.nfc")),
        "nfc_device_load() failed\r\n");

    NfcDevice* nfc_device_default_cap = nfc_device_alloc();
    mu_assert(
        nfc_device_load(nfc_device_default_cap, EXT_PATH("unit_tests/nfc/Slix_cap_default.nfc")),
        "nfc_device_load() failed\r\n");

    mu_assert(
        nfc_device_is_equal(nfc_device_missed_cap, nfc_device_default_cap),
        "nfc_device_is_equal() failed\r\n");

    nfc_device_free(nfc_device_default_cap);
    nfc_device_free(nfc_device_missed_cap);
}

NfcCommand slix_poller_set_password_callback(NfcGenericEventEx event, void* context) {
    furi_check(event.poller);
    furi_check(event.parent_event_data);
    furi_check(context);

    NfcCommand command = NfcCommandContinue;
    Iso15693_3PollerEvent* iso15_event = event.parent_event_data;
    SlixPoller* poller = event.poller;
    NfcTestSlixPollerSetPasswordContext* slix_ctx = context;

    if(iso15_event->type == Iso15693_3PollerEventTypeReady) {
        iso15693_3_copy(
            poller->data->iso15693_3_data, iso15693_3_poller_get_data(poller->iso15693_3_poller));

        if(slix_ctx->state == NfcTestSlixPollerSetPasswordStateGetRandomNumber) {
            slix_ctx->error = slix_poller_get_random_number(poller, &slix_ctx->random_number);
            if(slix_ctx->error != SlixErrorNone) {
                furi_thread_flags_set(slix_ctx->thread_id, NFC_TEST_FLAG_WORKER_DONE);
                command = NfcCommandStop;
            } else {
                slix_ctx->state = NfcTestSlixPollerSetPasswordStateSetPassword;
            }
        } else if(slix_ctx->state == NfcTestSlixPollerSetPasswordStateSetPassword) {
            slix_ctx->error = slix_poller_set_password(
                poller, SlixPasswordTypeRead, slix_ctx->password, slix_ctx->random_number);
            furi_thread_flags_set(slix_ctx->thread_id, NFC_TEST_FLAG_WORKER_DONE);
            command = NfcCommandStop;
        }
    } else {
        slix_ctx->error = slix_process_iso15693_3_error(iso15_event->data->error);
        furi_thread_flags_set(slix_ctx->thread_id, NFC_TEST_FLAG_WORKER_DONE);
        command = NfcCommandStop;
    }

    return command;
}

static void slix_set_password_test(const char* file_path, SlixPassword pass, bool correct_pass) {
    FURI_LOG_I(TAG, "Testing file: %s", file_path);

    Nfc* poller = nfc_alloc();
    Nfc* listener = nfc_alloc();

    NfcDevice* nfc_device = nfc_device_alloc();
    mu_assert(nfc_device_load(nfc_device, file_path), "nfc_device_load() failed\r\n");

    const SlixData* slix_data = nfc_device_get_data(nfc_device, NfcProtocolSlix);
    NfcListener* slix_listener = nfc_listener_alloc(listener, NfcProtocolSlix, slix_data);
    nfc_listener_start(slix_listener, NULL, NULL);

    SlixCapabilities slix_capabilities = slix_data->capabilities;

    NfcPoller* slix_poller = nfc_poller_alloc(poller, NfcProtocolSlix);

    NfcTestSlixPollerSetPasswordContext slix_poller_context = {
        .thread_id = furi_thread_get_current_id(),
        .state = NfcTestSlixPollerSetPasswordStateGetRandomNumber,
        .password = pass,
        .error = SlixErrorNone,
    };

    nfc_poller_start_ex(slix_poller, slix_poller_set_password_callback, &slix_poller_context);

    uint32_t flag =
        furi_thread_flags_wait(NFC_TEST_FLAG_WORKER_DONE, FuriFlagWaitAny, FuriWaitForever);
    mu_assert(flag == NFC_TEST_FLAG_WORKER_DONE, "Wrong thread flag\r\n");

    nfc_poller_stop(slix_poller);
    nfc_poller_free(slix_poller);
    nfc_listener_stop(slix_listener);
    nfc_listener_free(slix_listener);

    mu_assert(
        slix_poller_context.state == NfcTestSlixPollerSetPasswordStateSetPassword,
        "Poller failed before setting password\r\n");

    if((slix_capabilities == SlixCapabilitiesAcceptAllPasswords) || (correct_pass)) {
        mu_assert(slix_poller_context.error == SlixErrorNone, "Failed to set password\r\n");
    } else {
        mu_assert(
            slix_poller_context.error == SlixErrorTimeout,
            "Must have received SlixErrorTimeout\r\n");
    }

    nfc_device_free(nfc_device);
    nfc_free(listener);
    nfc_free(poller);
}

MU_TEST(slix_set_password_default_cap_correct_pass) {
    slix_set_password_test(EXT_PATH("unit_tests/nfc/Slix_cap_default.nfc"), 0x00000000, true);
}

MU_TEST(slix_set_password_default_cap_incorrect_pass) {
    slix_set_password_test(EXT_PATH("unit_tests/nfc/Slix_cap_default.nfc"), 0x12341234, false);
}

MU_TEST(slix_set_password_access_all_passwords_cap) {
    slix_set_password_test(
        EXT_PATH("unit_tests/nfc/Slix_cap_accept_all_pass.nfc"), 0x12341234, false);
}

// ---- ISO15693 ReadMultipleBlocks listener response (FNFC-01.3) ----------
// The listener response is checked byte by byte against a frame built here from the dump.
// The mock NFC layer limits frames to 64 bytes (poller buffer), so at most 15 blocks per read.

typedef enum {
    NfcTestRequestReadMulti, // READ_MULTIPLE_BLOCKS (default)
    NfcTestRequestReadSingle, // READ_SINGLE_BLOCK, block_count_minus_one must be 0
    NfcTestRequestGetRandom, // SLIX GET_RANDOM_NUMBER: flags + 2 random bytes, only the flags compared
} NfcTestRequest;

typedef struct {
    uint8_t first_block;
    uint8_t block_count_minus_one;
    bool option; // request block security status
    NfcTestRequest request;
} NfcTestReadMultiCase;

typedef struct {
    FuriThreadId thread_id;
    const SlixData* slix_data;
    bool read_protected_low; // blocks below the protection pointer are read protected
    const NfcTestReadMultiCase* cases;
    size_t case_count;
    size_t cases_passed;
    int failed_case;
} NfcTestReadMultiContext;

// Expected response without CRC, built independently of the listener code
static bool nfc_test_read_multi_expected(
    const NfcTestReadMultiContext* ctx,
    const NfcTestReadMultiCase* test_case,
    BitBuffer* expected) {
    const Iso15693_3Data* data = slix_get_base_data(ctx->slix_data);
    const uint32_t block_count = iso15693_3_get_block_count(data);
    const uint32_t first = test_case->first_block;
    const uint32_t last = MIN(first + test_case->block_count_minus_one, block_count - 1);

    bit_buffer_reset(expected);

    if(test_case->request == NfcTestRequestGetRandom) {
        bit_buffer_append_byte(expected, 0x00); // Response flags
        bit_buffer_append_bytes(
            expected, (const uint8_t[]){0x00, 0x00}, 2); // Random, not compared
        return true;
    }

    // first > block count: the listener stays silent (count wraps over the response limit)
    if(first > last + 1) return false;

    bool protected_block = false;
    for(uint32_t i = first; i <= last; ++i) {
        if(i == SLIX_COUNTER_BLOCK_NUM) continue;
        if(ctx->read_protected_low && i < ctx->slix_data->system_info.protection.pointer) {
            protected_block = true;
        }
    }

    if(protected_block) {
        bit_buffer_append_byte(expected, ISO15693_3_RESP_FLAG_ERROR);
        bit_buffer_append_byte(expected, ISO15693_3_RESP_ERROR_UNKNOWN);
        return true;
    }

    bit_buffer_append_byte(expected, 0x00); // Response flags
    for(uint32_t i = first; i <= last; ++i) {
        if(test_case->option) {
            bit_buffer_append_byte(expected, iso15693_3_is_block_locked(data, i) ? 1 : 0);
        }
        bit_buffer_append_bytes(
            expected, iso15693_3_get_block_data(data, i), iso15693_3_get_block_size(data));
    }
    return true;
}

NfcCommand nfc_test_read_multi_callback(NfcGenericEventEx event, void* context) {
    NfcTestReadMultiContext* ctx = context;
    Iso15693_3PollerEvent* iso15_event = event.parent_event_data;
    SlixPoller* poller = event.poller;

    // With read protection the poller's own activation (it reads every block) ends with
    // an error event instead of Ready. The field is still on, so the cases run either way.
    const bool poller_usable =
        (iso15_event->type == Iso15693_3PollerEventTypeReady) ||
        (ctx->read_protected_low && iso15_event->type == Iso15693_3PollerEventTypeError);

    if(poller_usable) {
        BitBuffer* tx = bit_buffer_alloc(16);
        BitBuffer* rx = bit_buffer_alloc(ISO15693_3_LISTENER_TEST_RX_SIZE);
        BitBuffer* expected = bit_buffer_alloc(ISO15693_3_LISTENER_TEST_RX_SIZE);

        for(size_t i = 0; i < ctx->case_count; ++i) {
            const NfcTestReadMultiCase* test_case = &ctx->cases[i];

            bit_buffer_reset(tx);
            bit_buffer_append_byte(
                tx,
                ISO15693_3_REQ_FLAG_DATA_RATE_HI |
                    (test_case->option ? ISO15693_3_REQ_FLAG_T4_OPTION : 0));
            if(test_case->request == NfcTestRequestGetRandom) {
                bit_buffer_append_byte(tx, SLIX_CMD_GET_RANDOM_NUMBER);
                bit_buffer_append_byte(tx, SLIX_NXP_MANUFACTURER_CODE);
            } else if(test_case->request == NfcTestRequestReadSingle) {
                bit_buffer_append_byte(tx, ISO15693_3_CMD_READ_BLOCK);
                bit_buffer_append_byte(tx, test_case->first_block);
            } else {
                bit_buffer_append_byte(tx, ISO15693_3_CMD_READ_MULTI_BLOCKS);
                bit_buffer_append_byte(tx, test_case->first_block);
                bit_buffer_append_byte(tx, test_case->block_count_minus_one);
            }

            const bool reply_expected = nfc_test_read_multi_expected(ctx, test_case, expected);
            const SlixError error = slix_poller_send_frame(poller, tx, rx, ISO15693_3_FDT_POLL_FC);
            // The random part of GET_RANDOM_NUMBER is not compared, only flags and length
            const size_t compare_size = (test_case->request == NfcTestRequestGetRandom) ?
                                            1 :
                                            bit_buffer_get_size_bytes(expected);

            const bool passed =
                reply_expected ?
                    (error == SlixErrorNone &&
                     bit_buffer_get_size_bytes(rx) == bit_buffer_get_size_bytes(expected) &&
                     memcmp(bit_buffer_get_data(rx), bit_buffer_get_data(expected), compare_size) ==
                         0) :
                    (error == SlixErrorTimeout);
            if(!passed) {
                ctx->failed_case = i;
                break;
            }
            ctx->cases_passed++;
        }

        bit_buffer_free(expected);
        bit_buffer_free(rx);
        bit_buffer_free(tx);
    }

    furi_thread_flags_set(ctx->thread_id, NFC_TEST_FLAG_WORKER_DONE);
    return NfcCommandStop;
}

static void nfc_test_read_multi_run(
    bool read_protected_low,
    const NfcTestReadMultiCase* cases,
    size_t case_count) {
    Nfc* poller = nfc_alloc();
    Nfc* listener = nfc_alloc();

    NfcDevice* nfc_device = nfc_device_alloc();
    mu_assert(
        nfc_device_load(nfc_device, EXT_PATH("unit_tests/nfc/Slix_cap_default.nfc")),
        "nfc_device_load() failed\r\n");

    SlixData* slix_data = slix_alloc();
    slix_copy(slix_data, nfc_device_get_data(nfc_device, NfcProtocolSlix));
    // The fixture has no read protection (condition 0x02 = write low)
    mu_assert(
        (slix_data->system_info.protection.condition &
         (SLIX_PP_CONDITION_RL | SLIX_PP_CONDITION_RH)) == 0,
        "fixture unexpectedly read protected\r\n");
    if(read_protected_low) {
        slix_data->system_info.protection.condition |= SLIX_PP_CONDITION_RL;
    }

    NfcListener* slix_listener = nfc_listener_alloc(listener, NfcProtocolSlix, slix_data);
    nfc_listener_start(slix_listener, NULL, NULL);

    NfcTestReadMultiContext ctx = {
        .thread_id = furi_thread_get_current_id(),
        .slix_data = slix_data,
        .read_protected_low = read_protected_low,
        .cases = cases,
        .case_count = case_count,
        .cases_passed = 0,
        .failed_case = -1,
    };

    NfcPoller* slix_poller = nfc_poller_alloc(poller, NfcProtocolSlix);
    nfc_poller_start_ex(slix_poller, nfc_test_read_multi_callback, &ctx);

    uint32_t flag =
        furi_thread_flags_wait(NFC_TEST_FLAG_WORKER_DONE, FuriFlagWaitAny, FuriWaitForever);
    mu_assert(flag == NFC_TEST_FLAG_WORKER_DONE, "Wrong thread flag\r\n");

    nfc_poller_stop(slix_poller);
    nfc_poller_free(slix_poller);
    nfc_listener_stop(slix_listener);
    nfc_listener_free(slix_listener);

    slix_free(slix_data);
    nfc_device_free(nfc_device);
    nfc_free(listener);
    nfc_free(poller);

    if(ctx.failed_case >= 0) {
        FURI_LOG_E(TAG, "ReadMultipleBlocks case %d differs", ctx.failed_case);
    }
    mu_assert(ctx.failed_case < 0, "ReadMultipleBlocks response differs\r\n");
    mu_assert(ctx.cases_passed == case_count, "Poller did not run all cases\r\n");
}

static void nfc_test_read_multi(bool read_protected_low) {
    static const NfcTestReadMultiCase cases[] = {
        {0, 0, false, NfcTestRequestReadMulti}, // 1 block
        {0, 7, false, NfcTestRequestReadMulti}, // 8 blocks
        {0, 14, false, NfcTestRequestReadMulti}, // 15 blocks (mock frame limit)
        {32, 14, false, NfcTestRequestReadMulti}, // above the protection pointer
        // clamped to the last block, includes the counter block
        {70, 14, false, NfcTestRequestReadMulti},
        {79, 0, false, NfcTestRequestReadMulti}, // counter block only
        {80, 0, false, NfcTestRequestReadMulti}, // first block == block count: flags only
        {81, 0, false, NfcTestRequestReadMulti}, // out of range: no reply
        {0, 0, true, NfcTestRequestReadMulti}, // with block security
        // 12 blocks with block security, across the protection pointer
        {20, 11, true, NfcTestRequestReadMulti},
        {75, 9, true, NfcTestRequestReadMulti}, // clamped, with block security
    };
    nfc_test_read_multi_run(read_protected_low, cases, COUNT_OF(cases));
}

MU_TEST(iso15693_3_listener_read_multi_blocks_test) {
    nfc_test_read_multi(false);
}

MU_TEST(iso15693_3_listener_read_multi_blocks_read_protected_test) {
    nfc_test_read_multi(true);
}

// Every reply shorter than the one before it: the tx buffers are reused between requests,
// so a reply must not carry the previous reply's bytes. Covers the standard reply, the
// standard error reply and the SLIX extension reply (its own tx buffer, copied in send_frame).
// Blocks below the protection pointer (32 in the fixture) are read protected here.
MU_TEST(iso15693_3_listener_short_after_long_test) {
    static const NfcTestReadMultiCase cases[] = {
        {32, 14, false, NfcTestRequestReadMulti}, // 15 blocks: 61 B
        {33, 0, false, NfcTestRequestReadSingle}, // 5 B
        {40, 11, true, NfcTestRequestReadMulti}, // 12 blocks with block security: 61 B
        {41, 0, true, NfcTestRequestReadSingle}, // with block security: 6 B
        {32, 14, false, NfcTestRequestReadMulti}, // 61 B
        {0, 0, false, NfcTestRequestReadMulti}, // read protected: error reply, 2 B
        {32, 14, false, NfcTestRequestReadMulti}, // 61 B
        {0, 0, false, NfcTestRequestGetRandom}, // SLIX extension: 3 B
        {32, 14, false, NfcTestRequestReadMulti}, // 61 B
        {80, 0, false, NfcTestRequestReadMulti}, // first block == block count: flags only, 1 B
        {33, 0, false, NfcTestRequestReadSingle}, // 5 B after 1 B: no stale bytes either way
    };
    nfc_test_read_multi_run(true, cases, COUNT_OF(cases));
}

// Bitwise ISO13239 CRC, as implemented before FNFC-01.3
static uint16_t nfc_test_iso13239_crc_reference(bool picopass, const uint8_t* data, size_t size) {
    uint16_t crc = picopass ? 0xE012U : 0xFFFFU;
    for(size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for(size_t j = 0; j < 8; ++j) {
            crc = (crc & 1U) ? (crc >> 1) ^ 0x8408U : (crc >> 1);
        }
    }
    return picopass ? crc : (uint16_t)~crc;
}

MU_TEST(iso13239_crc_table_test) {
    BitBuffer* buf = bit_buffer_alloc(ISO15693_3_LISTENER_TEST_CRC_MAX + ISO13239_CRC_SIZE);
    uint8_t* data = malloc(ISO15693_3_LISTENER_TEST_CRC_MAX);

    // Inventory response of a real SLIX2 card (PM3 trace): CRC is 0F 00
    const uint8_t inventory[] = {0x00, 0x01, 0xAC, 0x83, 0x9A, 0x01, 0x09, 0x01, 0x04, 0xE0};
    bit_buffer_copy_bytes(buf, inventory, sizeof(inventory));
    iso13239_crc_append(Iso13239CrcTypeDefault, buf);
    mu_assert(
        bit_buffer_get_byte(buf, 10) == 0x0F && bit_buffer_get_byte(buf, 11) == 0x00,
        "known inventory CRC mismatch\r\n");

    for(size_t i = 0; i < 400; ++i) {
        const size_t size = (i < 64) ? i :
                                       (furi_hal_random_get() % ISO15693_3_LISTENER_TEST_CRC_MAX);
        const bool picopass = i & 1U;
        const Iso13239CrcType type = picopass ? Iso13239CrcTypePicopass : Iso13239CrcTypeDefault;
        if(size) furi_hal_random_fill_buf(data, size); // fill_buf rejects 0 bytes

        bit_buffer_copy_bytes(buf, data, size);
        iso13239_crc_append(type, buf);

        const uint16_t expected = nfc_test_iso13239_crc_reference(picopass, data, size);
        mu_assert(
            bit_buffer_get_byte(buf, size) == (expected & 0xFFU) &&
                bit_buffer_get_byte(buf, size + 1) == (expected >> 8),
            "table CRC differs from bitwise CRC\r\n");
        if(size) {
            mu_assert(iso13239_crc_check(type, buf), "CRC check failed\r\n");
        }
    }

    free(data);
    bit_buffer_free(buf);
}

#define SLIX2_EAS_ON_FIXTURE EXT_PATH("unit_tests/nfc/slix2/SLIX2_eas_on.nfc")
#define SLIX2_NO_EAS_FIXTURE EXT_PATH("unit_tests/nfc/slix2/SLIX2_2026-02-27_16,08,34.nfc")
#define SLIX2_EAS_TMP        EXT_PATH("unit_tests/nfc/slix2_eas_tmp.nfc")

// FNFC-01.4: a dump with the EAS keys loads them; EAS ID "12 34" is on-air order (LSB
// first), i.e. the 16-bit value 0x3412, and is written back byte-for-byte.
// Checks are collected first and asserted after cleanup, so a failure leaks nothing.
MU_TEST(slix2_eas_fixture_roundtrip_test) {
    bool loaded = false, is_slix = false, eas_on = false, id_ok = false, eas_pp = false,
         afi_pp = true, saved = false, reloaded_ok = false, equal = false, id_key = false,
         id_bytes = false;

    NfcDevice* dev = nfc_device_alloc();
    loaded = nfc_device_load(dev, SLIX2_EAS_ON_FIXTURE);
    is_slix = loaded && nfc_device_get_protocol(dev) == NfcProtocolSlix;
    if(is_slix) {
        const SlixData* slix = nfc_device_get_data(dev, NfcProtocolSlix);
        eas_on = slix_is_eas_enabled(slix);
        id_ok = slix_get_eas_id(slix) == 0x3412;
        eas_pp = slix_is_eas_password_protected(slix);
        afi_pp = slix_is_afi_password_protected(slix);

        saved = nfc_device_save(dev, SLIX2_EAS_TMP);
        if(saved) {
            NfcDevice* reloaded = nfc_device_alloc();
            reloaded_ok = nfc_device_load(reloaded, SLIX2_EAS_TMP);
            equal = reloaded_ok && nfc_device_is_equal(dev, reloaded);
            nfc_device_free(reloaded);
        }
    }
    nfc_device_free(dev);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(saved) {
        FlipperFormat* ff = flipper_format_file_alloc(storage);
        uint8_t eas_id[2] = {};
        id_key = flipper_format_file_open_existing(ff, SLIX2_EAS_TMP) &&
                 flipper_format_read_hex(ff, "EAS ID", eas_id, 2);
        id_bytes = eas_id[0] == 0x12 && eas_id[1] == 0x34;
        flipper_format_free(ff);
    }
    storage_simply_remove(storage, SLIX2_EAS_TMP);
    furi_record_close(RECORD_STORAGE);

    mu_assert(loaded, "load EAS fixture failed\r\n");
    mu_assert(is_slix, "EAS fixture is not SLIX\r\n");
    mu_assert(eas_on, "EAS Mode must be true\r\n");
    mu_assert(id_ok, "EAS ID must be 0x3412 (12 34 LSB first)\r\n");
    mu_assert(eas_pp, "EAS must be password protected\r\n");
    mu_assert(!afi_pp, "AFI must not be password protected\r\n");
    mu_assert(saved, "save EAS fixture failed\r\n");
    mu_assert(reloaded_ok, "reload EAS fixture failed\r\n");
    mu_assert(equal, "EAS fixture round-trip not equal\r\n");
    mu_assert(id_key, "EAS ID key missing in saved file\r\n");
    mu_assert(id_bytes, "EAS ID not saved as 12 34\r\n");
}

// FNFC-01.4: an old dump without the EAS keys loads with defaults (EAS off, ID 00 00, no
// protections) and the saved file then carries all four keys with the default values.
MU_TEST(slix2_eas_defaults_test) {
    bool loaded = false, defaults = false, saved = false, opened = false;
    bool mode_key = false, id_key = false, eas_pp_key = false, afi_pp_key = false;
    bool mode = true, eas_pp = true, afi_pp = true;
    uint8_t eas_id[2] = {0xFF, 0xFF};

    NfcDevice* dev = nfc_device_alloc();
    loaded = nfc_device_load(dev, SLIX2_NO_EAS_FIXTURE) &&
             nfc_device_get_protocol(dev) == NfcProtocolSlix;
    if(loaded) {
        const SlixData* slix = nfc_device_get_data(dev, NfcProtocolSlix);
        defaults = !slix_is_eas_enabled(slix) && slix_get_eas_id(slix) == 0x0000 &&
                   !slix_is_eas_password_protected(slix) && !slix_is_afi_password_protected(slix);
        saved = nfc_device_save(dev, SLIX2_EAS_TMP);
    }
    nfc_device_free(dev);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(saved) {
        FlipperFormat* ff = flipper_format_file_alloc(storage);
        opened = flipper_format_file_open_existing(ff, SLIX2_EAS_TMP);
        if(opened) {
            mode_key = flipper_format_read_bool(ff, "EAS Mode", &mode, 1);
            id_key = flipper_format_read_hex(ff, "EAS ID", eas_id, 2);
            eas_pp_key = flipper_format_read_bool(ff, "EAS Password Protected", &eas_pp, 1);
            afi_pp_key = flipper_format_read_bool(ff, "AFI Password Protected", &afi_pp, 1);
        }
        flipper_format_free(ff);
    }
    storage_simply_remove(storage, SLIX2_EAS_TMP);
    furi_record_close(RECORD_STORAGE);

    mu_assert(loaded, "load dump without EAS keys failed\r\n");
    mu_assert(defaults, "EAS defaults must be false / 0000 / false / false\r\n");
    mu_assert(saved, "save dump failed\r\n");
    mu_assert(opened, "open saved file failed\r\n");
    mu_assert(mode_key && !mode, "saved EAS Mode must be false\r\n");
    mu_assert(id_key && eas_id[0] == 0x00 && eas_id[1] == 0x00, "saved EAS ID must be 00 00\r\n");
    mu_assert(eas_pp_key && !eas_pp, "saved EAS Password Protected must be false\r\n");
    mu_assert(afi_pp_key && !afi_pp, "saved AFI Password Protected must be false\r\n");
}

// FNFC-01.4: copy / is_equal / reset cover every EAS field (setters for the listener).
MU_TEST(slix2_eas_copy_equal_reset_test) {
    bool loaded = false, copy_equal = false, copy_id = false, mode_cmp = false,
         mode_restore = false, id_cmp = false, eas_pp_cmp = false, afi_pp_cmp = false,
         pp_restore = false, reset_clear = false;

    NfcDevice* dev = nfc_device_alloc();
    SlixData* copy = slix_alloc();
    loaded = nfc_device_load(dev, SLIX2_EAS_ON_FIXTURE) &&
             nfc_device_get_protocol(dev) == NfcProtocolSlix;
    if(loaded) {
        const SlixData* src = nfc_device_get_data(dev, NfcProtocolSlix);

        slix_reset(copy);
        slix_copy(copy, src);
        copy_equal = slix_is_equal(copy, src);
        copy_id = slix_get_eas_id(copy) == 0x3412;

        slix_set_eas(copy, false);
        mode_cmp = !slix_is_equal(copy, src);
        slix_set_eas(copy, true);
        mode_restore = slix_is_equal(copy, src);

        slix_set_eas_id(copy, 0x3413);
        id_cmp = !slix_is_equal(copy, src);
        slix_set_eas_id(copy, 0x3412);

        slix_set_eas_afi_protection(copy, false, false);
        eas_pp_cmp = !slix_is_equal(copy, src);
        slix_set_eas_afi_protection(copy, true, true);
        afi_pp_cmp = !slix_is_equal(copy, src);
        slix_set_eas_afi_protection(copy, true, false);
        pp_restore = slix_is_equal(copy, src);

        slix_reset(copy);
        reset_clear = !slix_is_eas_enabled(copy) && slix_get_eas_id(copy) == 0 &&
                      !slix_is_eas_password_protected(copy) &&
                      !slix_is_afi_password_protected(copy);
    }
    slix_free(copy);
    nfc_device_free(dev);

    mu_assert(loaded, "load EAS fixture failed\r\n");
    mu_assert(copy_equal, "copy must be equal\r\n");
    mu_assert(copy_id, "copy lost EAS ID\r\n");
    mu_assert(mode_cmp, "EAS Mode not compared\r\n");
    mu_assert(mode_restore, "restoring EAS Mode must make it equal again\r\n");
    mu_assert(id_cmp, "EAS ID not compared\r\n");
    mu_assert(eas_pp_cmp, "EAS protection not compared\r\n");
    mu_assert(afi_pp_cmp, "AFI protection not compared\r\n");
    mu_assert(pp_restore, "restoring protections must make it equal again\r\n");
    mu_assert(reset_clear, "reset must clear all EAS fields\r\n");
}

// AES-CMAC known-answer vectors from RFC 4493 (Appendix D.1), NIST AES-128 key. Exercises
// the empty / single-complete-block / partial-final / multi-block paths of the CMAC plus the
// cmac8 odd-index truncation used by MIFARE Plus SL3.
MU_TEST(mf_plus_crypto_cmac_rfc4493) {
    const uint8_t key[16] = {
        0x2b,
        0x7e,
        0x15,
        0x16,
        0x28,
        0xae,
        0xd2,
        0xa6,
        0xab,
        0xf7,
        0x15,
        0x88,
        0x09,
        0xcf,
        0x4f,
        0x3c};
    const uint8_t msg[64] = {0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96, 0xe9, 0x3d, 0x7e,
                             0x11, 0x73, 0x93, 0x17, 0x2a, 0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03,
                             0xac, 0x9c, 0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51, 0x30,
                             0xc8, 0x1c, 0x46, 0xa3, 0x5c, 0xe4, 0x11, 0xe5, 0xfb, 0xc1, 0x19,
                             0x1a, 0x0a, 0x52, 0xef, 0xf6, 0x9f, 0x24, 0x45, 0xdf, 0x4f, 0x9b,
                             0x17, 0xad, 0x2b, 0x41, 0x7b, 0xe6, 0x6c, 0x37, 0x10};
    const uint8_t exp0[16] = {
        0xbb,
        0x1d,
        0x69,
        0x29,
        0xe9,
        0x59,
        0x37,
        0x28,
        0x7f,
        0xa3,
        0x7d,
        0x12,
        0x9b,
        0x75,
        0x67,
        0x46};
    const uint8_t exp16[16] = {
        0x07,
        0x0a,
        0x16,
        0xb4,
        0x6b,
        0x4d,
        0x41,
        0x44,
        0xf7,
        0x9b,
        0xdd,
        0x9d,
        0xd0,
        0x4a,
        0x28,
        0x7c};
    const uint8_t exp40[16] = {
        0xdf,
        0xa6,
        0x67,
        0x47,
        0xde,
        0x9a,
        0xe6,
        0x30,
        0x30,
        0xca,
        0x32,
        0x61,
        0x14,
        0x97,
        0xc8,
        0x27};
    const uint8_t exp64[16] = {
        0x51,
        0xf0,
        0xbe,
        0xbf,
        0x7e,
        0x3b,
        0x9d,
        0x92,
        0xfc,
        0x49,
        0x74,
        0x17,
        0x79,
        0x36,
        0x3c,
        0xfe};
    uint8_t mac[16];

    mf_plus_crypto_cmac(key, msg, 0, mac);
    mu_assert(memcmp(mac, exp0, 16) == 0, "CMAC RFC4493 len=0 mismatch");
    mf_plus_crypto_cmac(key, msg, 16, mac);
    mu_assert(memcmp(mac, exp16, 16) == 0, "CMAC RFC4493 len=16 mismatch");
    mf_plus_crypto_cmac(key, msg, 40, mac);
    mu_assert(memcmp(mac, exp40, 16) == 0, "CMAC RFC4493 len=40 mismatch");
    mf_plus_crypto_cmac(key, msg, 64, mac);
    mu_assert(memcmp(mac, exp64, 16) == 0, "CMAC RFC4493 len=64 mismatch");

    // cmac8 = the 8 odd-indexed bytes of the full CMAC (checked on the len=16 vector).
    const uint8_t exp8[8] = {0x0a, 0xb4, 0x4d, 0x44, 0x9b, 0x9d, 0x4a, 0x7c};
    uint8_t mac8[8];
    mf_plus_crypto_cmac8(key, msg, 16, mac8);
    mu_assert(memcmp(mac8, exp8, 8) == 0, "cmac8 RFC4493 len=16 mismatch");
}

// Locks the data-encryption IV byte layout to the reference fork's (U-Prox-validated) layout: both
// IVs pack the little-endian {R_ctr, W_ctr} word {R_lo, R_hi, W_lo, W_hi}. Read repeats it across
// IV[0..11] with TI at IV[12..15]; write puts TI at IV[0..3] then repeats it across IV[4..15].
MU_TEST(mf_plus_crypto_data_iv_layout) {
    const uint8_t ti[4] = {0xa1, 0xa2, 0xa3, 0xa4};
    uint8_t iv[16];

    // Read-only session (W_ctr == 0, R_ctr < 256) reduces to the hardware-validated single-low-byte
    // form: R_lo at [0]/[4]/[8], TI at [12..15].
    const uint8_t exp_read[16] = {
        0x05,
        0x00,
        0x00,
        0x00,
        0x05,
        0x00,
        0x00,
        0x00,
        0x05,
        0x00,
        0x00,
        0x00,
        0xa1,
        0xa2,
        0xa3,
        0xa4};
    mf_plus_crypto_build_read_iv(ti, 0x0005, 0x0000, iv);
    mu_assert(memcmp(iv, exp_read, 16) == 0, "read IV layout mismatch");

    // Write IV (R_ctr == 0, W_ctr == 0x0007): TI at [0..3], W_lo at [6]/[10]/[14].
    const uint8_t exp_write[16] = {
        0xa1,
        0xa2,
        0xa3,
        0xa4,
        0x00,
        0x00,
        0x07,
        0x00,
        0x00,
        0x00,
        0x07,
        0x00,
        0x00,
        0x00,
        0x07,
        0x00};
    mf_plus_crypto_build_write_iv(ti, 0x0000, 0x0007, iv);
    mu_assert(memcmp(iv, exp_write, 16) == 0, "write IV layout mismatch");

    // Both counters pack little-endian in full: distinct R_ctr/W_ctr bytes land at fixed positions
    // in the repeated {R_lo, R_hi, W_lo, W_hi} word.
    const uint8_t exp_read_full[16] = {
        0x02,
        0x01,
        0x04,
        0x03,
        0x02,
        0x01,
        0x04,
        0x03,
        0x02,
        0x01,
        0x04,
        0x03,
        0xa1,
        0xa2,
        0xa3,
        0xa4};
    mf_plus_crypto_build_read_iv(ti, 0x0102, 0x0304, iv);
    mu_assert(memcmp(iv, exp_read_full, 16) == 0, "read IV counter-word mismatch");

    const uint8_t exp_write_full[16] = {
        0xa1,
        0xa2,
        0xa3,
        0xa4,
        0x02,
        0x01,
        0x04,
        0x03,
        0x02,
        0x01,
        0x04,
        0x03,
        0x02,
        0x01,
        0x04,
        0x03};
    mf_plus_crypto_build_write_iv(ti, 0x0102, 0x0304, iv);
    mu_assert(memcmp(iv, exp_write_full, 16) == 0, "write IV counter-word mismatch");
}

MU_TEST(iso15693_signal_encode_equivalence) {
    nfc_iso15693_signal_encode_equivalence_test();
}

MU_TEST(iso15693_signal_tick_exact) {
    nfc_iso15693_signal_tick_exact_test();
}

MU_TEST_SUITE(nfc) {
    nfc_test_alloc();

    MU_RUN_TEST(iso14443_3a_reader);
    MU_RUN_TEST(mf_ultralight_11_reader);
    MU_RUN_TEST(mf_ultralight_21_reader);
    MU_RUN_TEST(ntag_215_reader);
    MU_RUN_TEST(ntag_216_reader);
    MU_RUN_TEST(ntag_213_locked_reader);
    MU_RUN_TEST(mf_ultralight_c_reader);

    MU_RUN_TEST(mf_ultralight_write);

    MU_RUN_TEST(iso14443_3a_4b_file_test);
    MU_RUN_TEST(iso14443_3a_7b_file_test);

    MU_RUN_TEST(mf_ultralight_file_test);
    MU_RUN_TEST(mf_ultralight_ev1_11_file_test);
    MU_RUN_TEST(mf_ultralight_ev1_h11_file_test);
    MU_RUN_TEST(mf_ultralight_ev1_21_file_test);
    MU_RUN_TEST(mf_ultralight_ev1_h21_file_test);
    MU_RUN_TEST(mf_ultralight_ntag_203_file_test);
    MU_RUN_TEST(mf_ultralight_ntag_213_file_test);
    MU_RUN_TEST(mf_ultralight_ntag_215_file_test);
    MU_RUN_TEST(mf_ultralight_ntag_216_file_test);
    MU_RUN_TEST(mf_ultralight_ntag_i2c_1k_file_test);
    MU_RUN_TEST(mf_ultralight_ntag_i2c_2k_file_test);
    MU_RUN_TEST(mf_ultralight_ntag_i2c_plus_1k_file_test);
    MU_RUN_TEST(mf_ultralight_ntag_i2c_plus_2k_file_test);

    MU_RUN_TEST(mf_classic_mini_file_test);
    MU_RUN_TEST(mf_classic_1k_4b_file_test);
    MU_RUN_TEST(mf_classic_1k_7b_file_test);
    MU_RUN_TEST(mf_classic_4k_4b_file_test);
    MU_RUN_TEST(mf_classic_4k_7b_file_test);

    MU_RUN_TEST(mf_classic_reader);
    MU_RUN_TEST(mf_classic_write);
    MU_RUN_TEST(mf_classic_value_block);
    MU_RUN_TEST(mf_classic_send_frame_test);
    MU_RUN_TEST(mf_classic_dict_test);
    MU_RUN_TEST(felica_read);
    MU_RUN_TEST(felica_read_auth);
    MU_RUN_TEST(felica_standard_file_test);
    MU_RUN_TEST(felica_standard_v2_file_test);
    MU_RUN_TEST(felica_standard_v3_file_test);
    MU_RUN_TEST(felica_oversized_lite_file_test);
    MU_RUN_TEST(felica_standard_read);

    MU_RUN_TEST(slix_file_with_capabilities_test);
    MU_RUN_TEST(slix_set_password_default_cap_correct_pass);
    MU_RUN_TEST(slix_set_password_default_cap_incorrect_pass);
    MU_RUN_TEST(slix_set_password_access_all_passwords_cap);
    MU_RUN_TEST(slix2_eas_fixture_roundtrip_test);
    MU_RUN_TEST(slix2_eas_defaults_test);
    MU_RUN_TEST(slix2_eas_copy_equal_reset_test);

    MU_RUN_TEST(iso15693_signal_encode_equivalence);
    MU_RUN_TEST(iso15693_signal_tick_exact);
    MU_RUN_TEST(iso13239_crc_table_test);
    MU_RUN_TEST(iso15693_3_listener_read_multi_blocks_test);
    MU_RUN_TEST(iso15693_3_listener_read_multi_blocks_read_protected_test);
    MU_RUN_TEST(iso15693_3_listener_short_after_long_test);

    MU_RUN_TEST(mf_plus_crypto_cmac_rfc4493);
    MU_RUN_TEST(mf_plus_crypto_data_iv_layout);

    nfc_test_free();
}

int run_minunit_test_nfc(void) {
    MU_RUN_SUITE(nfc);
    return MU_EXIT_CODE;
}

TEST_API_DEFINE(run_minunit_test_nfc)
