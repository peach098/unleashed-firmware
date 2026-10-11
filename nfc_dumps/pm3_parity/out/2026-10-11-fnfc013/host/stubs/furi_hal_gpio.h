#pragma once
#include <stdint.h>
typedef struct { volatile uint32_t BSRR; } GPIO_TypeDef;
typedef struct { GPIO_TypeDef* port; uint16_t pin; } GpioPin;
typedef enum { GpioModeOutputPushPull } GpioMode;
typedef enum { GpioPullNo } GpioPull;
typedef enum { GpioSpeedVeryHigh } GpioSpeed;
static inline void furi_hal_gpio_init(const GpioPin* p, GpioMode m, GpioPull u, GpioSpeed s) { (void)p; (void)m; (void)u; (void)s; }
#define GPIO_BSRR_BS0_Pos 0
#define GPIO_BSRR_BR0_Pos 16
