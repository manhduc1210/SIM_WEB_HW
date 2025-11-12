
/**
 * @file hal_uart.h
 * @brief Portable UART HAL API (OS-agnostic).
 *
 * This header defines a small, portable UART interface that your application
 * can use without caring about the underlying OS or hardware. Each OS/board
 * supplies its own backend implementation (e.g., hal_uart_linux.c).
 *
 * For Linux, the backend uses termios/poll. For other OSes (uC/OS-III,
 * FreeRTOS, bare-metal), create corresponding backend files and keep this API.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HAL_Uart HAL_Uart;

typedef enum {
    HAL_UART_OK = 0,     ///< Operation succeeded
    HAL_UART_EINVAL = 1, ///< Invalid parameter or state
    HAL_UART_EIO    = 2, ///< I/O error
    HAL_UART_ECFG   = 3  ///< Configuration error (e.g., unsupported baud)
} HAL_UartStatus;

typedef enum {
    HAL_UART_PARITY_NONE = 0,
    HAL_UART_PARITY_EVEN,
    HAL_UART_PARITY_ODD
} HAL_UartParity;

/**
 * @brief UART configuration parameters.
 *
 * NOTE: This API targets typical 8-N-1 usage but allows parity/stop-bit changes.
 * On Linux, hardware flow control requires the port to support RTS/CTS.
 */
typedef struct {
    const char* device;     ///< e.g., "/dev/ttyPS1" or "/dev/ttyUSB0"
    uint32_t    baud;       ///< e.g., 115200
    uint8_t     data_bits;  ///< 5..8 (most common: 8)
    uint8_t     stop_bits;  ///< 1 or 2
    HAL_UartParity parity;  ///< NONE / EVEN / ODD
    uint8_t     non_blocking; ///< 0: blocking open; 1: O_NONBLOCK open
    uint8_t     hw_flow;    ///< 0: no RTS/CTS; 1: enable RTS/CTS if supported
} HAL_UartConfig;

/**
 * @brief Open a UART device with the given configuration.
 * @param cfg        Configuration (must not be NULL).
 * @param out_status Optional pointer to receive status code.
 * @return Handle on success; NULL on failure (out_status set if provided).
 */
HAL_Uart* HAL_Uart_Open(const HAL_UartConfig* cfg, HAL_UartStatus* out_status);

/**
 * @brief Close and free a UART handle.
 */
void HAL_Uart_Close(HAL_Uart* h);

/**
 * @brief Write exactly @p len bytes (best-effort loop).
 * @return Number of bytes written (>=0) or <0 on error.
 */
long HAL_Uart_Write(HAL_Uart* h, const void* buf, size_t len);

/**
 * @brief Convenience helper to write a zero-terminated C string.
 * @return Number of bytes written (>=0) or <0 on error.
 */
long HAL_Uart_WriteString(HAL_Uart* h, const char* s);

/**
 * @brief Read up to @p len bytes with a timeout.
 * @param timeout_ms 0 => non-blocking poll once; 0xFFFFFFFF => block forever.
 * @return Number of bytes read (0 on timeout), or <0 on error.
 */
long HAL_Uart_Read(HAL_Uart* h, void* buf, size_t len, uint32_t timeout_ms);

/**
 * @brief Flush UART buffers.
 * @param which 0=input, 1=output, 2=both
 */
HAL_UartStatus HAL_Uart_Flush(HAL_Uart* h, int which);

/**
 * @brief Get underlying file descriptor (Linux-only). Returns -1 if unsupported.
 */
int HAL_Uart_GetFd(HAL_Uart* h);

#ifdef __cplusplus
}
#endif
