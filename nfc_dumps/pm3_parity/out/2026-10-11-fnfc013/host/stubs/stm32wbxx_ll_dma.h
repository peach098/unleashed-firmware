#pragma once
#include <stdint.h>
typedef struct { uint32_t PeriphOrM2MSrcAddress, MemoryOrM2MDstAddress, Direction, Mode, PeriphOrM2MSrcIncMode, MemoryOrM2MDstIncMode, PeriphOrM2MSrcDataSize, MemoryOrM2MDstDataSize, NbData, PeriphRequest, Priority; } LL_DMA_InitTypeDef;
typedef struct { int dummy; } DMA_TypeDef;
extern DMA_TypeDef host_dma1;
#define DMA1 (&host_dma1)
enum { LL_DMA_CHANNEL_1, LL_DMA_CHANNEL_2, LL_DMA_DIRECTION_MEMORY_TO_PERIPH, LL_DMA_MODE_CIRCULAR, LL_DMA_PERIPH_NOINCREMENT, LL_DMA_MEMORY_INCREMENT, LL_DMA_PDATAALIGN_WORD, LL_DMA_MDATAALIGN_WORD, LL_DMAMUX_REQ_TIM2_UP, LL_DMA_PRIORITY_VERYHIGH, LL_DMA_PRIORITY_HIGH };
static inline int LL_DMA_Init(DMA_TypeDef* d, uint32_t c, LL_DMA_InitTypeDef* i) { (void)d; (void)c; (void)i; return 0; }
static inline void LL_DMA_EnableChannel(DMA_TypeDef* d, uint32_t c) { (void)d; (void)c; }
static inline void LL_DMA_DisableChannel(DMA_TypeDef* d, uint32_t c) { (void)d; (void)c; }
static inline void LL_DMA_ClearFlag_TC1(DMA_TypeDef* d) { (void)d; }
static inline void LL_DMA_ClearFlag_TC2(DMA_TypeDef* d) { (void)d; }
static inline uint32_t LL_DMA_GetDataLength(DMA_TypeDef* d, uint32_t c) { (void)d; (void)c; return 0; }
