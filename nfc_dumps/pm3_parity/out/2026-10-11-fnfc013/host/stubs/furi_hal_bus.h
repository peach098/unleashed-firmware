#pragma once
typedef enum { FuriHalBusTIM2 } FuriHalBus;
static inline void furi_hal_bus_enable(FuriHalBus b) { (void)b; }
static inline void furi_hal_bus_disable(FuriHalBus b) { (void)b; }
