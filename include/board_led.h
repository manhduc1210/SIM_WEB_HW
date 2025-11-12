#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void BoardLed_Init(void);

/* API bật/tắt toàn bộ LED */
void BoardLed_Set(uint8_t on);

/* API ghi theo mặt nạ 8 bit (bit=1 -> LED ON) */
void BoardLed_WriteMask(uint8_t mask);

/* API ghi một LED theo chỉ số (0..7) */
void BoardLed_WriteOne(unsigned index, uint8_t on);

#ifdef __cplusplus
}
#endif
