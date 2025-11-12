#include "osal.h"
#include "osal_task.h"
#include "board_led.h"
#include <stdint.h>

OSAL_TaskHandle hBlink = NULL;
OSAL_TaskHandle hLog   = NULL;

static void BlinkTask(void* arg) {
    (void)arg;
    uint8_t state = 0;
    BoardLed_Init();
    for (;;) {
        state ^= 1u;
        BoardLed_Set(state);
        OSAL_LOG("[Blink] LED=%s\r\n", state ? "ON" : "OFF");
        OSAL_TaskDelayMs(500);
    }
}

static void LogTask(void* arg) {
    (void)arg;
    uint32_t ms = 0;
    for (;;) {
        ms += 2000;
        OSAL_LOG("[Log] uptime=%u ms\r\n", (unsigned)ms);
        OSAL_TaskDelayMs(2000);
    }
}

//2s suspend Blink, 3s resume Blink
static void CtrlTask(void* arg) {
    (void)arg;
    for (;;) {
        OSAL_LOG("[Ctrl] Suspend Blink\r\n");
        OSAL_TaskSuspend(hBlink);
        OSAL_TaskDelayMs(5000);
        OSAL_LOG("[Ctrl] Resume Blink\r\n");
        OSAL_TaskResume(hBlink);
        OSAL_TaskDelayMs(4000);
    }
}

void Demo1_Start(void) {
    OSAL_Status s1,s2,s3;
    OSAL_TaskHandle hCtrl = NULL;

    // uC/OS-III: Blink < Log < Ctrl
    OSAL_TaskAttr a1 = { .name="BlinkTask", .stack_size=2048, .prio=15 };
    OSAL_TaskAttr a2 = { .name="LogTask",   .stack_size=2048, .prio=20 };
    OSAL_TaskAttr a3 = { .name="CtrlTask",  .stack_size=2048, .prio=25 };

    s1 = OSAL_TaskCreate(&hBlink, BlinkTask, NULL, &a1);
    s2 = OSAL_TaskCreate(&hLog,   LogTask,   NULL, &a2);
    s3 = OSAL_TaskCreate(&hCtrl,  CtrlTask,  NULL, &a3);

    OSAL_LOG("[Demo1] Create Blink=%d, Log=%d, Ctrl=%d (handles: %p %p %p)\r\n",
             s1, s2, s3, (void*)hBlink, (void*)hLog, (void*)hCtrl);
}
