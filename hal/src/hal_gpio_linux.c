/**
 * @file hal_gpio_linux.c
 * @brief Linux backend for HAL GPIO using libgpiod v1.x
 *
 * Build: link with -lgpiod
 */

#include "hal_gpio.h"
#include <stdio.h>
#include <gpiod.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct HAL_GpioChip {
    struct gpiod_chip* chip;
    char name[64];
};

typedef struct {
    uint32_t debounce_ms;
    uint64_t last_evt_ns;
} _HalDebounce;

struct HAL_GpioLine {
    HAL_GpioChip*        hchip;
    struct gpiod_line*   line;
    HAL_GpioLineConfig   cfg;
    int                  have_event;    /* 1 if requested with events */
    _HalDebounce         db;
};

/* --- helpers --- */

uint64_t _timespec_to_ns(const struct timespec* ts) {
    if (!ts) return 0;
    return ((uint64_t)ts->tv_sec * 1000000000ull) + (uint64_t)ts->tv_nsec;
}

/* Map logical value to physical considering active low/high */
int _logical_to_physical(const HAL_GpioLineConfig* c, int logical) {
    return (c->active == HAL_GPIO_ACTIVE_LOW) ? (!logical) : (logical != 0);
}

/* Map physical read to logical */
int _physical_to_logical(const HAL_GpioLineConfig* c, int physical) {
    int v = physical ? 1 : 0;
    return (c->active == HAL_GPIO_ACTIVE_LOW) ? !v : v;
}

/* Resolve by name if provided (libgpiod v1: iterate) */
static int _resolve_offset_by_name(struct gpiod_chip* chip, const char* name) {
    if (!name || !*name) return -1;
    int num = gpiod_chip_num_lines(chip);
    for (int off = 0; off < num; ++off) {
        struct gpiod_line* ln = gpiod_chip_get_line(chip, off);
        if (!ln) continue;
        const char* ln_name = gpiod_line_name(ln);
        if (ln_name && strcmp(ln_name, name) == 0) {
            gpiod_line_release(ln); /* release temp ref */
            return off;
        }
        gpiod_line_release(ln);
    }
    return -1;
}

/* --- API impl --- */

HAL_GpioStatus HAL_GpioChip_Open(const HAL_GpioChipConfig* cfg, HAL_GpioChip** out_chip) {
    if (!cfg || !cfg->chip_name || !cfg->chip_name[0] || !out_chip) {
        printf("[GPIO][LINUX] invalid chip config (name missing)\r\n");
        return HAL_GPIO_EINVAL;
    }
    HAL_GpioChip* hc = (HAL_GpioChip*)calloc(1, sizeof(*hc));
    if (!hc) return HAL_GPIO_EIO;

    hc->chip = gpiod_chip_open_by_name(cfg->chip_name);
    if (!hc->chip) {
        printf("[GPIO][LINUX] gpiod_chip_open_by_name('%s') failed\r\n", cfg->chip_name);
        free(hc);
        return HAL_GPIO_EIO;
    }
    strncpy(hc->name, cfg->chip_name, sizeof(hc->name)-1);
    printf("[GPIO][LINUX] chip opened: %s\r\n", hc->name);
    *out_chip = hc;
    return HAL_GPIO_OK;
}

void HAL_GpioChip_Close(HAL_GpioChip* chip) {
    if (!chip) return;
    if (chip->chip) gpiod_chip_close(chip->chip);
    free(chip);
}

HAL_GpioStatus HAL_GpioLine_Request(HAL_GpioChip* chip, const HAL_GpioLineConfig* cfg, HAL_GpioLine** out_line) {
    if (!chip || !chip->chip || !cfg || !out_line) return HAL_GPIO_EINVAL;

    int offset = cfg->offset;
    // printf("offset : %d/r/n",offset);
    if (offset < 0 && cfg->name) {
        offset = _resolve_offset_by_name(chip->chip, cfg->name);
        if (offset < 0) {
            printf("[GPIO][LINUX] line '%s' not found on %s\r\n", cfg->name, chip->name);
            return HAL_GPIO_ENOENT;
        }
    }
    if (offset < 0) return HAL_GPIO_EINVAL;

    if (!chip->chip) {
        printf("open chip failed\n");
    }
    else{
        printf("open chip passed\n");
    }
    printf("offset : %d\r\n",offset);

    struct gpiod_line* ln = gpiod_chip_get_line(chip->chip, offset);
    printf("ln : %d\r\n",ln);
    if (!ln) {
        printf("ln : %d\r\n",ln);
        return HAL_GPIO_EIO;
    }

    /* Request */
    int rc = 0;
    if (cfg->dir == HAL_GPIO_DIR_OUT) {
        int phys_init = _logical_to_physical(cfg, cfg->initial);
        rc = gpiod_line_request_output(ln, "hal_gpio", phys_init);
    } else {
        if (cfg->edge == HAL_GPIO_EDGE_NONE) {
            rc = gpiod_line_request_input(ln, "hal_gpio");
        } else if (cfg->edge == HAL_GPIO_EDGE_RISING) {
            rc = gpiod_line_request_rising_edge_events(ln, "hal_gpio");
        } else if (cfg->edge == HAL_GPIO_EDGE_FALLING) {
            rc = gpiod_line_request_falling_edge_events(ln, "hal_gpio");
        } else {
            rc = gpiod_line_request_both_edges_events(ln, "hal_gpio");
        }
    }
    if (rc < 0) {
        gpiod_line_release(ln);
        return HAL_GPIO_EIO;
    }

    HAL_GpioLine* h = (HAL_GpioLine*)calloc(1, sizeof(*h));
    if (!h) { gpiod_line_release(ln); return HAL_GPIO_EIO; }
    h->hchip      = chip;
    h->line       = ln;
    h->cfg        = *cfg;
    h->have_event = (cfg->dir == HAL_GPIO_DIR_IN && cfg->edge != HAL_GPIO_EDGE_NONE) ? 1 : 0;
    h->db.debounce_ms = cfg->debounce_ms;
    h->db.last_evt_ns = 0;

    *out_line = h;
    return HAL_GPIO_OK;
}

void HAL_GpioLine_Release(HAL_GpioLine* line) {
    if (!line) return;
    if (line->line) gpiod_line_release(line->line);
    free(line);
}

HAL_GpioStatus HAL_GpioLine_Write(HAL_GpioLine* line, int value) {
    if (!line || !line->line) return HAL_GPIO_EINVAL;
    if (line->cfg.dir != HAL_GPIO_DIR_OUT) return HAL_GPIO_EINVAL;
    int phys = _logical_to_physical(&line->cfg, value);
    return (gpiod_line_set_value(line->line, phys) < 0) ? HAL_GPIO_EIO : HAL_GPIO_OK;
}

HAL_GpioStatus HAL_GpioLine_Toggle(HAL_GpioLine* line) {
    if (!line || !line->line) return HAL_GPIO_EINVAL;
    int phys = gpiod_line_get_value(line->line);
    if (phys < 0) return HAL_GPIO_EIO;
    int logi = _physical_to_logical(&line->cfg, phys);
    return HAL_GpioLine_Write(line, !logi);
}

HAL_GpioStatus HAL_GpioLine_Read(HAL_GpioLine* line, int* out) {
    if (!line || !line->line || !out) return HAL_GPIO_EINVAL;
    int phys = gpiod_line_get_value(line->line);
    if (phys < 0) return HAL_GPIO_EIO;
    *out = _physical_to_logical(&line->cfg, phys);
    return HAL_GPIO_OK;
}

HAL_GpioStatus HAL_GpioLine_WaitEvent(HAL_GpioLine* line, int timeout_ms, HAL_GpioEvent* out_ev) {
    if (!line || !line->line) return HAL_GPIO_EINVAL;
    if (!line->have_event)    return HAL_GPIO_ENOSUP;

    struct timespec ts = {0,0};
    int rc = gpiod_line_event_wait(line->line,
                                   (timeout_ms < 0) ? NULL :
                                   (&(struct timespec){ .tv_sec = timeout_ms/1000, .tv_nsec = (timeout_ms%1000)*1000000 }));
    if (rc < 0) return HAL_GPIO_EIO;        /* error */
    if (rc == 0) return HAL_GPIO_ENOENT;    /* timeout */

    struct gpiod_line_event ev;
    if (gpiod_line_event_read(line->line, &ev) < 0) return HAL_GPIO_EIO;

    uint64_t t_ns = _timespec_to_ns(&ev.ts);
    /* Soft debounce */
    if (line->db.debounce_ms > 0 && line->db.last_evt_ns != 0) {
        uint64_t dt = (t_ns > line->db.last_evt_ns) ? (t_ns - line->db.last_evt_ns) : 0;
        if (dt < (uint64_t)line->db.debounce_ms * 1000000ull) {
            /* Drop event */
            return HAL_GPIO_ENOENT;
        }
    }
    line->db.last_evt_ns = t_ns;

    HAL_GpioEdge ed = HAL_GPIO_EDGE_NONE;
    if (ev.event_type == GPIOD_LINE_EVENT_RISING_EDGE)  ed = HAL_GPIO_EDGE_RISING;
    if (ev.event_type == GPIOD_LINE_EVENT_FALLING_EDGE) ed = HAL_GPIO_EDGE_FALLING;
    if (out_ev) {
        out_ev->timestamp_ns = t_ns;
        out_ev->edge         = ed;
    }
    return HAL_GPIO_OK;
}

/* --- Group helpers (simple loops; no libgpiod bulk dependency) --- */
HAL_GpioStatus HAL_GpioGroup_WriteMask(HAL_GpioGroup* grp, uint32_t mask, uint32_t value) {
    if (!grp || !grp->lines) return HAL_GPIO_EINVAL;
    for (size_t i = 0; i < grp->count; ++i) {
        if (mask & (1u << i)) {
            int bit = (value >> i) & 1u;
            HAL_GpioLine_Write(grp->lines[i], bit);
        }
    }
    return HAL_GPIO_OK;
}

HAL_GpioStatus HAL_GpioGroup_ReadBitmap(HAL_GpioGroup* grp, uint32_t* out_bitmap) {
    if (!grp || !grp->lines || !out_bitmap) return HAL_GPIO_EINVAL;
    uint32_t bm = 0;
    for (size_t i = 0; i < grp->count; ++i) {
        int v=0;
        if (HAL_GpioLine_Read(grp->lines[i], &v) == HAL_GPIO_OK && v) bm |= (1u << i);
    }
    *out_bitmap = bm;
    return HAL_GPIO_OK;
}
