// board_led_linux_gpiod.c
#include "board_led.h"
#include "osal.h"
#include <gpiod.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef OSAL_GPIOD_MAX
#define OSAL_GPIOD_MAX 8
#endif

typedef struct {
    const char* chip_name;   // ví dụ: "gpiochip0"
    unsigned    line_base;   // offset bắt đầu (ví dụ 0 hoặc 32)
    unsigned    count;       // số LED, mặc định 8
} OSAL_GpiodCtx;

static struct gpiod_chip*     s_chip  = NULL;
static struct gpiod_line_bulk s_bulk;
static struct gpiod_line*     s_lines[OSAL_GPIOD_MAX]; /* truy cập từng line nhanh */
static unsigned               s_count = 0;

static int _ints_all(int *dst, int v, unsigned n){
    for (unsigned i=0;i<n;i++) dst[i]=v;
    return 0;
}

static void _fill_vals_from_mask(int* vals, uint8_t mask, unsigned n) {
    for (unsigned i = 0; i < n; ++i) vals[i] = (mask >> i) & 0x1;
}

void BoardLed_Init(void)
{
    OSAL_GpiodCtx* ctx = (OSAL_GpiodCtx*)g_osal.cfg.platform_ctx;

    const char* chip_name = (ctx && ctx->chip_name) ? ctx->chip_name : "gpiochip0";
    unsigned line_base    = (ctx) ? ctx->line_base : 0;
    s_count               = (ctx && ctx->count) ? ctx->count : 8;
    if (s_count > OSAL_GPIOD_MAX) s_count = OSAL_GPIOD_MAX;

    s_chip = gpiod_chip_open_by_name(chip_name);
    if (!s_chip) {
        OSAL_LOG("[LED][GPIOD] open %s failed\r\n", chip_name);
        return;
    }

    unsigned offs[OSAL_GPIOD_MAX];
    for (unsigned i=0;i<s_count;i++) offs[i] = line_base + i;

    // if (gpiod_line_bulk_init(&s_bulk) != 0) {
    //     OSAL_LOG("[LED][GPIOD] bulk init failed\r\n");
    //     return;
    // }
    gpiod_line_bulk_init(&s_bulk);

    if (gpiod_chip_get_lines(s_chip, offs, s_count, &s_bulk) != 0) {
        OSAL_LOG("[LED][GPIOD] get lines failed (base=%u, count=%u)\r\n", line_base, s_count);
        return;
    }
    int zeros[OSAL_GPIOD_MAX]; 
    _ints_all(zeros, 0, s_count);
    if (gpiod_line_request_bulk_output(&s_bulk, "osal_led", zeros) != 0) {
        OSAL_LOG("[LED][GPIOD] request output failed\r\n");
        return;
    }
    OSAL_LOG("[LED][GPIOD] ready on %s, base=%u, count=%u\r\n", chip_name, line_base, s_count);
}

void BoardLed_Set(uint8_t on)
{
    if (!s_chip) return;
    int vals[OSAL_GPIOD_MAX];
    for (unsigned i=0;i<s_count;i++) vals[i] = on ? 1 : 0;
    gpiod_line_set_value_bulk(&s_bulk, vals);
}

/* API ghi theo mặt nạ 8-bit */
void BoardLed_WriteMask(uint8_t mask)
{
    if (!s_chip) return;
    int vals[OSAL_GPIOD_MAX];
    _fill_vals_from_mask(vals, mask, s_count);

    if (gpiod_line_set_value_bulk(&s_bulk, vals) == 0) return;

    /* fallback từng line */
    for (unsigned i = 0; i < s_count; ++i)
        if (s_lines[i]) gpiod_line_set_value(s_lines[i], vals[i]);
}

/* API ghi một LED theo chỉ số */
void BoardLed_WriteOne(unsigned index, uint8_t on)
{
    if (!s_chip || index >= s_count) return;
    if (s_lines[index])
        gpiod_line_set_value(s_lines[index], on ? 1 : 0);
}

/*
BoardLed_Set(1);  // all ON
// ví dụ: chỉ bật LED0, LED3, LED7
BoardLed_WriteMask( (1u<<0) | (1u<<3) | (1u<<7) );
BoardLed_WriteOne(3, 1);  // bật LED3
BoardLed_WriteOne(3, 0);  // tắt LED3
*/