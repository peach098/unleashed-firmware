#pragma once
#include <stdint.h>
typedef struct { volatile uint32_t ARR; } TIM_TypeDef;
extern TIM_TypeDef host_tim2;
#define TIM2 (&host_tim2)
enum { LL_TIM_COUNTERMODE_UP, LL_TIM_CLOCKDIVISION_DIV1 };
#define LL_TIM_SetCounterMode(t, m) ((void)(t))
#define LL_TIM_SetClockDivision(t, m) ((void)(t))
#define LL_TIM_SetPrescaler(t, m) ((void)(t))
#define LL_TIM_SetAutoReload(t, m) ((void)(t))
#define LL_TIM_SetCounter(t, m) ((void)(t))
#define LL_TIM_EnableCounter(t) ((void)(t))
#define LL_TIM_DisableCounter(t) ((void)(t))
#define LL_TIM_EnableUpdateEvent(t) ((void)(t))
#define LL_TIM_DisableUpdateEvent(t) ((void)(t))
#define LL_TIM_EnableDMAReq_UPDATE(t) ((void)(t))
#define LL_TIM_DisableDMAReq_UPDATE(t) ((void)(t))
#define LL_TIM_GenerateEvent_UPDATE(t) ((void)(t))
