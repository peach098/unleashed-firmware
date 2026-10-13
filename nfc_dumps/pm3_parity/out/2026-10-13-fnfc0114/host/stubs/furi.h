#pragma once
// FNFC-01.14 host stub: the parts of furi.h that iso15693_parser.c and bit_buffer.c use
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
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef UNUSED
#define UNUSED(x) (void)(x)
#endif
#define FURI_LOG_D(...) do {} while(0)
#define FURI_LOG_I(...) do {} while(0)
#define FURI_LOG_E(...) do {} while(0)
extern int host_critical_depth;
#define FURI_CRITICAL_ENTER() do { host_critical_depth++; } while(0)
#define FURI_CRITICAL_EXIT() do { host_critical_depth--; } while(0)
#define FURI_BIT(x, n) (((x) >> (n)) & 1U)
