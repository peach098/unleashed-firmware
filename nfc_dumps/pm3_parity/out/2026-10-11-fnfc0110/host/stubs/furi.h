#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "core/check.h"
#define malloc(x) calloc(1, (x))
#ifndef COUNT_OF
#define COUNT_OF(x) (sizeof(x) / sizeof(x[0]))
#endif
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef UNUSED
#define UNUSED(x) (void)(x)
#endif
#define FURI_LOG_D(...) do {} while(0)
#define FURI_LOG_I(...) do {} while(0)
#define FURI_CRITICAL_ENTER() do {} while(0)
#define FURI_CRITICAL_EXIT() do {} while(0)
typedef struct { volatile uint32_t CYCCNT; } DWT_Type;
extern DWT_Type host_dwt;
#define DWT (&host_dwt)
#define FURI_BIT(x, n) (((x) >> (n)) & 1U)
