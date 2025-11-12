#pragma once
#include "hal_gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Demo configuration (kept minimal and generic).
 * You pass offsets for LEDs (LSB at index 0) and two buttons.
 */
typedef struct {
    const char* chip_name;     // e.g. "gpiochip0"
    int         led_offsets[8];
    int         led_count;     // 1..8
    int         btn0_offset;   // increment
    int         btn1_offset;   // reset
    int         leds_active_low; // 1 if LEDs are active-low
    int         btns_active_low; // 1 if buttons active-low (pressed=0)
    int         debounce_ms;     // software debounce for buttons
} DemoGpioCfg;

void DemoGpio_Start(const DemoGpioCfg* cfg);
void DemoGpio_Stop(void);

#ifdef __cplusplus
}
#endif
