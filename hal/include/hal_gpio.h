/**
 * @file hal_gpio.h
 * @brief General-purpose GPIO HAL (portable header, OS-agnostic).
 *
 * Notes:
 *  - Public API is OS-neutral. Linux backend uses libgpiod v1.x, but this
 *    header does not mention libgpiod to keep portability.
 *  - Model: Chip → Lines (single) and optional Groups (convenience).
 */

#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HAL_GpioChip HAL_GpioChip;
typedef struct HAL_GpioLine HAL_GpioLine;

typedef enum {
    HAL_GPIO_OK = 0,
    HAL_GPIO_EINVAL = 1,
    HAL_GPIO_EIO = 2,
    HAL_GPIO_ENOSUP = 3,
    HAL_GPIO_ENOENT = 4
} HAL_GpioStatus;

typedef enum { HAL_GPIO_DIR_IN = 0, HAL_GPIO_DIR_OUT = 1 } HAL_GpioDir;
typedef enum { HAL_GPIO_ACTIVE_HIGH = 0, HAL_GPIO_ACTIVE_LOW = 1 } HAL_GpioActive;
typedef enum { HAL_GPIO_DRIVE_PUSHPULL = 0, HAL_GPIO_DRIVE_OPENDRAIN, HAL_GPIO_DRIVE_OPENSOURCE } HAL_GpioDrive;
typedef enum { HAL_GPIO_BIAS_AS_IS = 0, HAL_GPIO_BIAS_PULL_UP, HAL_GPIO_BIAS_PULL_DOWN, HAL_GPIO_BIAS_DISABLE } HAL_GpioBias;
typedef enum { HAL_GPIO_EDGE_NONE = 0, HAL_GPIO_EDGE_RISING, HAL_GPIO_EDGE_FALLING, HAL_GPIO_EDGE_BOTH } HAL_GpioEdge;

typedef struct {
    const char* chip_name;           ///< e.g. "gpiochip0"
} HAL_GpioChipConfig;

/** Single line configuration (offset or name identifies a line). */
typedef struct {
    int         offset;              ///< use >=0 if known; otherwise -1 and set name
    const char* name;                ///< optional line label (Linux); can be NULL
    HAL_GpioDir    dir;
    HAL_GpioActive active;
    HAL_GpioDrive  drive;            ///< may be ignored if backend doesn't support
    HAL_GpioBias   bias;             ///< may be ignored if backend doesn't support
    int            initial;          ///< initial output value (0/1) when dir=OUT
    HAL_GpioEdge   edge;             ///< when dir=IN, request edge events if != NONE
    uint32_t       debounce_ms;      ///< soft debounce in HAL (0 = disabled)
} HAL_GpioLineConfig;

/* Chip lifetime */
HAL_GpioStatus HAL_GpioChip_Open (const HAL_GpioChipConfig* cfg, HAL_GpioChip** out_chip);
void           HAL_GpioChip_Close(HAL_GpioChip* chip);

/* Line lifetime */
HAL_GpioStatus HAL_GpioLine_Request (HAL_GpioChip* chip, const HAL_GpioLineConfig* cfg, HAL_GpioLine** out_line);
void           HAL_GpioLine_Release(HAL_GpioLine* line);

/* Basic I/O */
HAL_GpioStatus HAL_GpioLine_Write (HAL_GpioLine* line, int value); /* logical value (active-aware) */
HAL_GpioStatus HAL_GpioLine_Toggle(HAL_GpioLine* line);
HAL_GpioStatus HAL_GpioLine_Read  (HAL_GpioLine* line, int* out_value); /* logical (pressed/high=1) */

/* Event wait (for inputs requested with edge != NONE). timeout_ms: -1=forever, 0=non-blocking */
typedef struct {
    uint64_t     timestamp_ns;  ///< 0 if not provided by backend
    HAL_GpioEdge edge;          ///< which edge fired
} HAL_GpioEvent;

HAL_GpioStatus HAL_GpioLine_WaitEvent(HAL_GpioLine* line, int timeout_ms, HAL_GpioEvent* out_ev);

/* Convenience: Groups (array of lines) */
typedef struct { HAL_GpioLine** lines; size_t count; } HAL_GpioGroup;

HAL_GpioStatus HAL_GpioGroup_WriteMask (HAL_GpioGroup* grp, uint32_t mask, uint32_t value);
HAL_GpioStatus HAL_GpioGroup_ReadBitmap(HAL_GpioGroup* grp, uint32_t* out_bitmap);

#ifdef __cplusplus
}
#endif
