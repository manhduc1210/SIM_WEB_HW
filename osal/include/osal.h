#pragma once
#include "osal_types.h"

typedef struct {
    OSAL_Backend backend;
    OSAL_LogFn   log;           // ví dụ: xil_printf
    void*        platform_ctx;  // ví dụ: con trỏ GIC
} OSAL_Config;

typedef struct {
    OSAL_Config cfg;
    uint8_t     initialized;
} OSAL_Global;

extern OSAL_Global g_osal;

#ifdef __cplusplus
extern "C" {
#endif

OSAL_Status OSAL_Init(const OSAL_Config *cfg);
void        OSAL_Deinit(void);

#ifndef OSAL_LOG
#  define OSAL_LOG(...) do { if (g_osal.cfg.log) g_osal.cfg.log(__VA_ARGS__); } while(0)
#endif

#ifdef __cplusplus
}
#endif
