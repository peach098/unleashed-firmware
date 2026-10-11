/**
 * @file digital_signal_i.h
 * @brief DigitalSignal private definitions.
 *
 * This file is an implementation detail. It must not be included in
 * any public API-related headers.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/*
 * Internal time unit: 5 ps, half of the 10 ps unit of the public API, so that one period of the
 * 64 MHz timer (15.625 ns) is a whole number of units and the conversion is exact.
 */
#define DIGITAL_SIGNAL_T_TIM      3125 /**< 15.625 ns in 5 ps units */
#define DIGITAL_SIGNAL_T_TIM_DIV2 1562 /**< (T_TIM - 1) / 2: the largest remainder rounded down */

/**
 * @brief DigitalSignal structure type.
 */
struct DigitalSignal {
    bool start_level; /**< The level to begin the signal with. */
    uint32_t size; /**< Current period count contained in the instance. */
    uint32_t max_size; /**< Maximum period count this instance can hold. */
    /** Remainder left after converting all periods into timer ticks, in 5 ps units,
     * within [-DIGITAL_SIGNAL_T_TIM_DIV2, DIGITAL_SIGNAL_T_TIM_DIV2]. */
    int32_t remainder;
    uint32_t data[]; /**< The array of time periods. */
};
