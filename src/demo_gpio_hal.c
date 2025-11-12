/**
 * @file demo_gpio_hal.c
 * @brief Demo: BTN0 increments LED counter (up to 255), BTN1 resets.
 * Uses generalized HAL GPIO (line-based). Polling with soft debounce.
 */
#include "demo_gpio_hal.h"
#include "osal.h"
#include "osal_task.h"

#include <string.h>
#include <stdlib.h>

static HAL_GpioChip*   s_chip    = NULL;
static HAL_GpioLine*   s_leds[8] = {0};
static int             s_led_n   = 0;
static HAL_GpioLine*   s_btn0    = NULL;
static HAL_GpioLine*   s_btn1    = NULL;

static volatile int    s_run     = 0;
static OSAL_TaskHandle s_task    = NULL;
static unsigned        s_count   = 0;  // 0..255

static void _leds_show8(unsigned val) {
    for (int i = 0; i < s_led_n; ++i) {
        HAL_GpioLine_Write(s_leds[i], (val >> i) & 1u);
    }
}

static void GpioTask(void* arg) {
    (void)arg;

    const int step_ms = 5;
    const int debounce_ms = ((const DemoGpioCfg*)arg)->debounce_ms > 0 ? ((const DemoGpioCfg*)arg)->debounce_ms : 5;

    int last0 = 0, last1 = 0;
    int stable0 = 0, stable1 = 0;
    int acc0 = 0, acc1 = 0;

    _leds_show8(s_count);

    while (s_run) {
        int v0=0, v1=0;
        HAL_GpioLine_Read(s_btn0, &v0);
        HAL_GpioLine_Read(s_btn1, &v1);

        // debounce BTN0
        if (v0 == last0) { acc0 += step_ms; if (acc0 >= debounce_ms) stable0 = v0; }
        else             { acc0 = 0; }
        // debounce BTN1
        if (v1 == last1) { acc1 += step_ms; if (acc1 >= debounce_ms) stable1 = v1; }
        else             { acc1 = 0; }

        // rising edges
        static int prev0 = 0, prev1 = 0;
        int rising0 = (stable0 && !prev0);
        int rising1 = (stable1 && !prev1);

        if (rising0) {
            if (s_count < 255) s_count++;
            OSAL_LOG("[GPIO][BTN0] ++ -> %u\r\n", s_count);
            _leds_show8(s_count);
        }
        if (rising1) {
            s_count = 0;
            OSAL_LOG("[GPIO][BTN1] reset -> %u\r\n", s_count);
            _leds_show8(s_count);
        }

        prev0 = stable0;
        prev1 = stable1;
        last0 = v0;
        last1 = v1;

        OSAL_TaskDelayMs(step_ms);
    }
    OSAL_LOG("[DemoGPIO] task exit\r\n");
}

void DemoGpio_Start(const DemoGpioCfg* cfg) {
    if (!cfg || !cfg->chip_name || cfg->led_count <= 0 || cfg->led_count > 8) {
        OSAL_LOG("[DemoGPIO] invalid cfg\r\n");
        return;
    }

    /* 1) Open chip */
    HAL_GpioChipConfig cc = { .chip_name = cfg->chip_name };
    if (HAL_GpioChip_Open(&cc, &s_chip) != HAL_GPIO_OK) {
        OSAL_LOG("[DemoGPIO] chip open failed\r\n"); return;
    }

    /* 2) Request LEDs */
    s_led_n = cfg->led_count;
    for (int i = 0; i < s_led_n; ++i) {
        HAL_GpioLineConfig lc = {
            .offset  = cfg->led_offsets[i],
            .name    = NULL,
            .dir     = HAL_GPIO_DIR_OUT,
            .active  = cfg->leds_active_low ? HAL_GPIO_ACTIVE_LOW : HAL_GPIO_ACTIVE_HIGH,
            .drive   = HAL_GPIO_DRIVE_PUSHPULL,
            .bias    = HAL_GPIO_BIAS_AS_IS,
            .initial = 0,
            .edge    = HAL_GPIO_EDGE_NONE,
            .debounce_ms = 0
        };
        if (HAL_GpioLine_Request(s_chip, &lc, &s_leds[i]) != HAL_GPIO_OK) {
            OSAL_LOG("[DemoGPIO] LED line %d request failed\r\n", i);
            return;
        }
    }

    /* 3) Request BTN0 / BTN1 as inputs (no event API here; we'll poll) */
    HAL_GpioLineConfig bc = {
        .offset  = cfg->btn0_offset,
        .name    = NULL,
        .dir     = HAL_GPIO_DIR_IN,
        .active  = cfg->btns_active_low ? HAL_GPIO_ACTIVE_LOW : HAL_GPIO_ACTIVE_HIGH,
        .drive   = HAL_GPIO_DRIVE_PUSHPULL, /* ignored for input */
        .bias    = HAL_GPIO_BIAS_AS_IS,     /* or PULL_UP if supported */
        .initial = 0,
        .edge    = HAL_GPIO_EDGE_NONE,
        .debounce_ms = (uint32_t)cfg->debounce_ms
    };
    if (HAL_GpioLine_Request(s_chip, &bc, &s_btn0) != HAL_GPIO_OK) {
        // printf("status : %s", HAL_GpioLine_Request(s_chip, &bc, &s_btn0));
        OSAL_LOG("[DemoGPIO] BTN0 request failed\r\n"); 
        return;
    }
    bc.offset = cfg->btn1_offset;
    if (HAL_GpioLine_Request(s_chip, &bc, &s_btn1) != HAL_GPIO_OK) {
        // printf("status : %s", HAL_GpioLine_Request(s_chip, &bc, &s_btn1));
        OSAL_LOG("[DemoGPIO] BTN1 request failed\r\n"); return;
    }

    s_run = 1;
    static DemoGpioCfg s_cfg_copy; /* keep debounce value for task */
    s_cfg_copy = *cfg;
    OSAL_TaskAttr a = { .name="DemoGPIO", .stack_size=2048, .prio=18 };
    OSAL_TaskCreate(&s_task, GpioTask, &s_cfg_copy, &a);

    OSAL_LOG("[DemoGPIO] started (BTN0=+1..255, BTN1=reset)\r\n");
}

void DemoGpio_Stop(void) {
    s_run = 0;
    OSAL_TaskDelayMs(50);

    for (int i = 0; i < s_led_n; ++i) {
        if (s_leds[i]) { HAL_GpioLine_Release(s_leds[i]); s_leds[i] = NULL; }
    }
    s_led_n = 0;

    if (s_btn0) { HAL_GpioLine_Release(s_btn0); s_btn0 = NULL; }
    if (s_btn1) { HAL_GpioLine_Release(s_btn1); s_btn1 = NULL; }

    if (s_chip) { HAL_GpioChip_Close(s_chip); s_chip = NULL; }

    OSAL_LOG("[DemoGPIO] stopped\r\n");
}
