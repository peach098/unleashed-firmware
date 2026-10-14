#include "digital_signal.h"
#include "digital_signal_i.h"

#include <furi.h>

#define TAG "DigitalSignal"

DigitalSignal* digital_signal_alloc(uint32_t max_size) {
    DigitalSignal* signal = malloc(sizeof(DigitalSignal) + (max_size * sizeof(uint32_t)));

    signal->max_size = max_size;

    return signal;
}

void digital_signal_free(DigitalSignal* signal) {
    furi_check(signal);

    free(signal);
}

bool digital_signal_get_start_level(const DigitalSignal* signal) {
    furi_check(signal);

    return signal->start_level;
}

void digital_signal_set_start_level(DigitalSignal* signal, bool level) {
    furi_check(signal);

    signal->start_level = level;
}

uint32_t digital_signal_get_size(const DigitalSignal* signal) {
    furi_check(signal);

    return signal->size;
}

/*
 * Convert a duration in 10 ps units plus the remainder carried over from the previous period
 * (5 ps units) to whole timer ticks, rounded to the nearest tick. The new remainder is stored back.
 * One tick is DIGITAL_SIGNAL_T_TIM = 3125 units of 5 ps, so 3125 input units are exactly 2 ticks:
 * splitting the input at that point keeps every intermediate value in 32 bits for any input.
 */
static inline uint32_t digital_signal_ticks_to_reload(uint32_t ticks, int32_t* remainder) {
    const uint32_t tick_pairs = ticks / DIGITAL_SIGNAL_T_TIM;
    /* Rest in 5 ps units, offset by half a tick to round to the nearest: [0, 3 * T_TIM - 3] */
    const int32_t rest =
        (int32_t)(ticks % DIGITAL_SIGNAL_T_TIM) * 2 + *remainder + DIGITAL_SIGNAL_T_TIM_DIV2;
    const uint32_t rest_ticks = (uint32_t)rest / DIGITAL_SIGNAL_T_TIM;

    *remainder = rest - (int32_t)(rest_ticks * DIGITAL_SIGNAL_T_TIM) - DIGITAL_SIGNAL_T_TIM_DIV2;

    return tick_pairs * 2 + rest_ticks;
}

void digital_signal_add_period(DigitalSignal* signal, uint32_t ticks) {
    furi_check(signal);
    furi_check(signal->size < signal->max_size);

    int32_t remainder = signal->remainder;
    const uint32_t reload_value = digital_signal_ticks_to_reload(ticks, &remainder);

    furi_check(reload_value > 1);

    signal->data[signal->size++] = reload_value - 1;
    signal->remainder = remainder;
}

static void digital_signal_extend_last_period(DigitalSignal* signal, uint32_t ticks) {
    furi_assert(signal->size <= signal->max_size);

    /* The stored period is a whole number of ticks: only the added duration needs rounding */
    int32_t remainder = signal->remainder;
    const uint32_t reload_value =
        signal->data[signal->size - 1] + 1 + digital_signal_ticks_to_reload(ticks, &remainder);

    furi_check(reload_value > 1);

    signal->data[signal->size - 1] = reload_value - 1;
    signal->remainder = remainder;
}

void digital_signal_add_period_with_level(DigitalSignal* signal, uint32_t ticks, bool level) {
    furi_check(signal);

    if(signal->size == 0) {
        signal->start_level = level;
        digital_signal_add_period(signal, ticks);
    } else {
        const bool end_level = signal->start_level ^ !(signal->size % 2);

        if(level != end_level) {
            digital_signal_add_period(signal, ticks);
        } else {
            digital_signal_extend_last_period(signal, ticks);
        }
    }
}
