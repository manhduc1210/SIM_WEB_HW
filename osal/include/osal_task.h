#pragma once
#include "osal_types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*OSAL_TaskEntry)(void* arg);
typedef void* OSAL_TaskHandle;

typedef enum {
    OSAL_TASK_STATE_INVALID = 0,
    OSAL_TASK_STATE_READY,
    OSAL_TASK_STATE_RUNNING,
    OSAL_TASK_STATE_WAITING,
    OSAL_TASK_STATE_SUSPENDED,
    OSAL_TASK_STATE_COMPLETED,
} OSAL_TaskState;

typedef struct {
    const char* name;
    uint16_t    stack_size;   // bytes
    uint8_t     prio;         // 0 = cao nhất (theo RTOS)
} OSAL_TaskAttr;

/* ===== Core API ===== */
OSAL_Status OSAL_TaskCreate(OSAL_TaskHandle* h, OSAL_TaskEntry entry, void* arg, const OSAL_TaskAttr* attr);
OSAL_Status OSAL_TaskDelete(OSAL_TaskHandle h);
OSAL_Status OSAL_TaskSuspend(OSAL_TaskHandle h);
OSAL_Status OSAL_TaskResume(OSAL_TaskHandle h);
OSAL_Status OSAL_TaskChangePrio(OSAL_TaskHandle h, uint8_t new_prio);
OSAL_Status OSAL_TaskGetState(OSAL_TaskHandle h, OSAL_TaskState* state);
OSAL_Status OSAL_TaskGetName(OSAL_TaskHandle h, const char** name);
void        OSAL_TaskDelayMs(uint32_t ms);
void        OSAL_TaskYield(void);

/* ===== Utility ===== */
uint32_t    OSAL_TaskCount(void);
OSAL_Status OSAL_TaskForEach(void (*cb)(OSAL_TaskHandle h, void* arg), void* arg);

#ifdef __cplusplus
}
#endif
