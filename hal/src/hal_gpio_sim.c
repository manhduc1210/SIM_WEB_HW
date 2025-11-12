// hal_gpio_sim.c
#include <stdlib.h>
#include <string.h>
#include "hal_gpio.h"   // dùng lại header gốc

#define HAL_GPIO_SIM_MAX_LINES 64

typedef struct {
    int used;
    int offset;
    HAL_GpioDir   dir;
    HAL_GpioActive active;
    int value;          // 0/1 hiện tại
} HalGpioSimLine;

typedef struct {
    char name[32];
    int  line_count;
    HalGpioSimLine lines[HAL_GPIO_SIM_MAX_LINES];
} HalGpioSimChip;

/* --------- Helpers nội bộ ---------- */

static HalGpioSimLine* sim_find_line(HalGpioSimChip* chip, int offset)
{
    if (!chip) return NULL;
    for (int i = 0; i < chip->line_count; ++i) {
        if (chip->lines[i].offset == offset) {
            return &chip->lines[i];
        }
    }
    return NULL;
}

/* --------- API giống hal_gpio_linux.c ---------- */

HAL_GpioStatus HAL_GpioChip_Open(const HAL_GpioChipConfig* cfg, HAL_GpioChip** out_chip)
{
    if (!cfg || !out_chip) return HAL_GPIO_EINVAL;

    HalGpioSimChip* c = (HalGpioSimChip*)malloc(sizeof(HalGpioSimChip));
    if (!c) return HAL_GPIO_EIO;

    memset(c, 0, sizeof(*c));
    strncpy(c->name, cfg->chip_name ? cfg->chip_name : "sim-gpio", sizeof(c->name)-1);

    // giả lập có 32 line, offset 0..31
    c->line_count = 32;
    for (int i = 0; i < c->line_count; ++i) {
        c->lines[i].offset = i;
        c->lines[i].used   = 0;
        c->lines[i].dir    = HAL_GPIO_DIR_IN;
        c->lines[i].active = HAL_GPIO_ACTIVE_HIGH;
        c->lines[i].value  = 0;
    }

    *out_chip = (HAL_GpioChip*)c;
    return HAL_GPIO_OK;
}

void HAL_GpioChip_Close(HAL_GpioChip* chip)
{
    if (!chip) return;
    free(chip);
}

HAL_GpioStatus HAL_GpioLine_Request(HAL_GpioChip* chip,
                                    const HAL_GpioLineConfig* cfg,
                                    HAL_GpioLine** out_line)
{
    HalGpioSimChip* c = (HalGpioSimChip*)chip;
    if (!c || !cfg || !out_line) return HAL_GPIO_EINVAL;

    HalGpioSimLine* ln = sim_find_line(c, cfg->offset);
    if (!ln) return HAL_GPIO_ENOENT;

    // đánh dấu line này đã được dùng
    ln->used   = 1;
    ln->dir    = cfg->dir;
    ln->active = cfg->active;
    // nếu là output thì set initial
    if (cfg->dir == HAL_GPIO_DIR_OUT) {
        ln->value = cfg->initial ? 1 : 0;
    }

    *out_line = (HAL_GpioLine*)ln;
    return HAL_GPIO_OK;
}

void HAL_GpioLine_Release(HAL_GpioLine* line)
{
    if (!line) return;
    HalGpioSimLine* ln = (HalGpioSimLine*)line;
    ln->used = 0;
}

/* đọc từ line */
HAL_GpioStatus HAL_GpioLine_Read(HAL_GpioLine* line, int* out_val)
{
    if (!line || !out_val) return HAL_GPIO_EINVAL;
    HalGpioSimLine* ln = (HalGpioSimLine*)line;

    int v = ln->value;
    // nếu active low thì giá trị logic ngược lại
    if (ln->active == HAL_GPIO_ACTIVE_LOW) {
        v = v ? 0 : 1;
    }
    *out_val = v;
    return HAL_GPIO_OK;
}

/* ghi ra line */
HAL_GpioStatus HAL_GpioLine_Write(HAL_GpioLine* line, int val)
{
    if (!line) return HAL_GPIO_EINVAL;
    HalGpioSimLine* ln = (HalGpioSimLine*)line;

    if (ln->dir != HAL_GPIO_DIR_OUT) {
        return HAL_GPIO_EIO; // hoặc EINVAL
    }

    // nếu active low thì ghi ngược
    if (ln->active == HAL_GPIO_ACTIVE_LOW) {
        ln->value = val ? 0 : 1;
    } else {
        ln->value = val ? 1 : 0;
    }
    return HAL_GPIO_OK;
}

/* ---- Các hàm chỉ dùng cho SIM, không nhất thiết khai báo trong hal_gpio.h ---- */

/* Set giá trị cho 1 line input (mô phỏng người dùng ấn nút) */
HAL_GpioStatus HAL_GpioSim_SetInput(HAL_GpioChip* chip, int offset, int logic_val)
{
    HalGpioSimChip* c = (HalGpioSimChip*)chip;
    HalGpioSimLine* ln = sim_find_line(c, offset);
    if (!ln) return HAL_GPIO_ENOENT;

    // ép line này về input luôn cũng được
    ln->dir   = HAL_GPIO_DIR_IN;
    // lưu trực tiếp theo logic (chưa tính active)
    ln->value = logic_val ? 1 : 0;
    return HAL_GPIO_OK;
}

/* Lấy giá trị thực tế của 1 line output (để biết LED đang on/off) */
HAL_GpioStatus HAL_GpioSim_GetOutput(HAL_GpioChip* chip, int offset, int* out_logic)
{
    HalGpioSimChip* c = (HalGpioSimChip*)chip;
    HalGpioSimLine* ln = sim_find_line(c, offset);
    if (!ln || !out_logic) return HAL_GPIO_EINVAL;

    int v = ln->value;
    if (ln->active == HAL_GPIO_ACTIVE_LOW) {
        v = v ? 0 : 1;
    }
    *out_logic = v;
    return HAL_GPIO_OK;
}
