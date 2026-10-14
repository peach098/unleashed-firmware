#pragma once

#include <nfc/protocols/iso15693_3/iso15693_3_poller_i.h>

#include "slix_poller.h"

#define SLIX_POLLER_SET_PASSWORD_FWT (100000)

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SlixPollerStateIdle,
    SlixPollerStateGetNxpSysInfo,
    SlixPollerStateReadSignature,
    SlixPollerStateCheckEas,
    SlixPollerStateCheckPrivacyPassword,
    SlixPollerStatePrivacyUnlock,
    SlixPollerStateReady,
    SlixPollerStateError,
    SlixPollerStateNum,
} SlixPollerState;

struct SlixPoller {
    Iso15693_3Poller* iso15693_3_poller;
    SlixType type;
    SlixData* data;
    SlixPollerState poller_state;
    SlixError error;
    SlixRandomNumber random_number;
    bool privacy_password_checked;

    BitBuffer* tx_buffer;
    BitBuffer* rx_buffer;

    SlixPollerEventData slix_event_data;
    SlixPollerEvent slix_event;
    NfcGenericEvent general_event;
    NfcGenericCallback callback;
    void* context;
};

/**
 * @brief Send an addressed EAS ALARM (option flag 0) and report whether EAS mode is on.
 *
 * Must ONLY be used inside the callback function.
 *
 * Waits one ISO15693 FWT, like the other NXP custom commands. A card with EAS off is silent
 * or answers with an error: *active is false and the error is returned for logging only.
 *
 * @param[in, out] instance pointer to the instance to be used in the transaction.
 * @param[out] active set to true if the card answered with the 32-byte EAS sequence.
 * @return SlixErrorNone if the card answered without the error flag, an error code otherwise.
 */
SlixError slix_poller_eas_alarm(SlixPoller* instance, bool* active);

#ifdef __cplusplus
}
#endif
