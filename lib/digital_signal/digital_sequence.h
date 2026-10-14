/**
 * @file digital_sequence.h
 * @brief Fast and precise digital signal generation library.
 *
 * Each sequence is represented by one or more (up to 32) registered signals, which are addressed
 * by their indices, and a list of signal indices to be transmitted.
 *
 * The registered signals must be set up prior to actually defining the sequence.
 *
 * Example: A sequence containing 4 registered signals and n indices to transmit.
 *
 * |Signal | Index |
 * |:-----:|:-----:|
 * |  SOF  |   0   |
 * |  EOF  |   1   |
 * | Zero  |   2   |
 * |  One  |   3   |
 *
 * ```
 * Signal index | 0 | 3 | 2 | 2 | ... |   3   |   1   |
 *                0   1   2   3   ...   n - 2   n - 1
 * ```
 *
 * The above sequence starts by transmitting the signal with index 0, which is SOF in this case,
 * then it proceeds with indices 3, 2, 2, which are One, Zero, Zero and after n - 2 signals,
 * it will conclude with indices 3 and 1 which are One and EOF respectively.
 *
 * This way, only the order in which the signals are sent is stored, while the signals themselves
 * are not duplicated.
 */
#pragma once

#include <stddef.h>
#include <furi_hal_gpio.h>

#include <digital_signal/digital_signal.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DigitalSequence DigitalSequence;

/**
 * @brief Allocate a DigitalSequence instance of a given size which will operate on a set GPIO pin.
 *
 * @param[in] size maximum number of signal indices contained in the instance.
 * @param[in] gpio the GPIO pin used to generate the signal.
 * @returns pointer to the allocated DigitalSequence instance.
 */
DigitalSequence* digital_sequence_alloc(uint32_t size, const GpioPin* gpio);

/**
 * @brief Delete a previously allocated DigitalSequence instance.
 *
 * @param[in,out] sequence pointer to the instance to be deleted.
 */
void digital_sequence_free(DigitalSequence* sequence);

/**
 * @brief Register a signal within a DigitalSequence instance by its index.
 *
 * This function must be called for each signal to be used in the sequence. The DigitalSequence
 * instance does not own the signals, therefore, their lifetime must be no less than the instance's.
 *
 * The user is responsible for creation and deletion of DigitalSignal instances and
 * also for keeping track of their respective indices.
 *
 * @param[in,out] sequence pointer to the instance to be modified.
 * @param[in] signal_index index to register the signal under (must be less than 32).
 * @param[in] signal pointer to the DigitalSignal instance to be registered.
 */
void digital_sequence_register_signal(
    DigitalSequence* sequence,
    uint8_t signal_index,
    const DigitalSignal* signal);

/**
 * @brief Append a signal index to a DigitalSequence instance.
 *
 * The signal under the index must be registered beforehand by calling digital_sequence_set_signal().
 *
 * @param[in,out] sequence pointer to the instance to be modified.
 * @param[in] signal_index signal index to be appended to the sequence (must be less than 32).
 */
void digital_sequence_add_signal(DigitalSequence* sequence, uint8_t signal_index);

/**
 * @brief Lookup table for digital_sequence_add_bytes().
 *
 * Maps every 4-bit value to the four signal indices of its bits, bit 0 first,
 * packed into one little-endian word (the index of bit 0 is in the lowest byte).
 * Must be filled with digital_sequence_bit_table_init().
 */
typedef struct {
    uint32_t nibble[16];
} DigitalSequenceBitTable;

/**
 * @brief Fill a DigitalSequenceBitTable for a bit-serial encoding with one signal per bit.
 *
 * Intended to be called once, outside of time-critical code.
 *
 * @param[out] table pointer to the table to be filled.
 * @param[in] signal_index_zero signal index to transmit for a 0 bit (must be less than 32).
 * @param[in] signal_index_one signal index to transmit for a 1 bit (must be less than 32).
 */
void digital_sequence_bit_table_init(
    DigitalSequenceBitTable* table,
    uint8_t signal_index_zero,
    uint8_t signal_index_one);

/**
 * @brief Append one signal index per data bit to a DigitalSequence instance, bit 0 of each byte first.
 *
 * The result is identical to calling digital_sequence_add_signal() once per bit, but
 * the capacity is checked once and four indices are written at a time.
 *
 * @param[in,out] sequence pointer to the instance to be modified.
 * @param[in] table pointer to a table filled with digital_sequence_bit_table_init().
 * @param[in] data pointer to the data to be appended.
 * @param[in] data_size size of the data in bytes (appends 8 * data_size indices).
 */
void digital_sequence_add_bytes(
    DigitalSequence* sequence,
    const DigitalSequenceBitTable* table,
    const uint8_t* data,
    size_t data_size);

/**
 * @brief Set how many DMA ring buffer entries are filled before the transmission starts.
 *
 * By default the whole ring buffer is filled first. A smaller value reduces the delay between
 * calling digital_sequence_transmit() and the first edge. It is only safe when the
 * periods at the start of the sequence give the CPU enough time to keep the ring
 * buffer ahead of the DMA.
 *
 * @param[in,out] sequence pointer to the instance to be modified.
 * @param[in] prefill_size number of entries, clamped to the ring buffer capacity (must not be 0).
 */
void digital_sequence_set_prefill_size(DigitalSequence* sequence, uint32_t prefill_size);

/**
 * @brief Transmit the sequence contained in the DigitalSequence instance.
 *
 * Must contain at least one registered signal and one signal index.
 *
 * NOTE: The current implementation will properly initialise the GPIO provided during construction,
 * but it is the caller's responsibility to reconfigure it back before reusing for other purposes.
 * This is due to performance reasons.
 *
 * @param[in] sequence pointer to the sequence to be transmitted.
 */
void digital_sequence_transmit(DigitalSequence* sequence);

/**
 * @brief Clear the signal sequence in a DigitalSequence instance.
 *
 * Calling this function does not un-register the registered signals, so it is
 * safe to call digital_sequence_add_signal() right afterwards.
 *
 * @param[in,out] sequence pointer to the instance to be cleared.
 */
void digital_sequence_clear(DigitalSequence* sequence);

#ifdef __cplusplus
}
#endif
