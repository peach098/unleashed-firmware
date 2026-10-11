#pragma once
#include <stdio.h>
#include <stdlib.h>
#define furi_check(x) do { if(!(x)) { fprintf(stderr, "furi_check failed %s:%d: %s\n", __FILE__, __LINE__, #x); abort(); } } while(0)
#define furi_assert(x) furi_check(x)
#define furi_crash(msg) do { fprintf(stderr, "crash: %s\n", msg); abort(); } while(0)
