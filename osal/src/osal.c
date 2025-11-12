#include "osal.h"
#include <string.h>

OSAL_Global g_osal = {0};

OSAL_Status OSAL_Init(const OSAL_Config *cfg) {
    if (!cfg) return OSAL_EINVAL;
    memset(&g_osal, 0, sizeof(g_osal));
    g_osal.cfg = *cfg;
    g_osal.initialized = 1;
    OSAL_LOG("[OSAL] Init backend=%d\r\n", (int)cfg->backend);
    return OSAL_OK;
}

void OSAL_Deinit(void) { g_osal.initialized = 0; }
