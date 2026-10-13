#pragma once
#include <stdint.h>
typedef struct { void* port; uint16_t pin; } GpioPin;
typedef enum { GpioPullNo, GpioPullUp, GpioPullDown } GpioPull;
