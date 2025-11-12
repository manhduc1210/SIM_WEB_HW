
/**
 * @file hal_uart_linux.c
 * @brief Linux backend for HAL UART (termios + poll).
 *
 * This implementation targets POSIX/Linux using:
 *  - open/close/read/write for I/O
 *  - termios for line settings (baud, bits, parity, stop, flow control)
 *  - poll() for timed/non-blocking reads
 *
 * The code aims to be simple, robust, and suitable for task-based apps via OSAL.
 */
#include "hal_uart.h"
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <poll.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/** Internal UART handle. */
struct HAL_Uart {
    int fd;              ///< POSIX file descriptor
    HAL_UartConfig cfg;  ///< Saved config for reference
};

/** Convert an integer baud rate into a termios speed_t flag. */
static speed_t _baud_to_flag(uint32_t b) {
    switch (b) {
        case 50: return B50; case 75: return B75; case 110: return B110; case 134: return B134;
        case 150: return B150; case 200: return B200; case 300: return B300; case 600: return B600;
        case 1200: return B1200; case 1800: return B1800; case 2400: return B2400; case 4800: return B4800;
        case 9600: return B9600; case 19200: return B19200; case 38400: return B38400;
        case 57600: return B57600; case 115200: return B115200; case 230400: return B230400;
#ifdef B460800
        case 460800: return B460800;
#endif
#ifdef B921600
        case 921600: return B921600;
#endif
        default: return 0;
    }
}

/** Apply config (raw mode, parity/stop/data bits, flow control, baud). */
static int _apply_cfg(int fd, const HAL_UartConfig* c) {
    struct termios tio;
    if (tcgetattr(fd, &tio) != 0) return -1;

    // Start from a "raw" baseline (no line processing)
    cfmakeraw(&tio);

    // Data bits
    tio.c_cflag &= ~CSIZE;
    switch (c->data_bits) {
        case 5: tio.c_cflag |= CS5; break;
        case 6: tio.c_cflag |= CS6; break;
        case 7: tio.c_cflag |= CS7; break;
        default: /*8*/ tio.c_cflag |= CS8; break;
    }

    // Stop bits
    if (c->stop_bits == 2) tio.c_cflag |= CSTOPB;
    else                   tio.c_cflag &= ~CSTOPB;

    // Parity
    tio.c_cflag &= ~(PARENB|PARODD);
    if (c->parity == HAL_UART_PARITY_EVEN) {
        tio.c_cflag |= PARENB;
        tio.c_cflag &= ~PARODD;
    } else if (c->parity == HAL_UART_PARITY_ODD) {
        tio.c_cflag |= PARENB;
        tio.c_cflag |= PARODD;
    }

    // Hardware flow control (RTS/CTS), if available
#ifdef CRTSCTS
    if (c->hw_flow) tio.c_cflag |= CRTSCTS;
    else            tio.c_cflag &= ~CRTSCTS;
#endif

    // Enable receiver, ignore modem control lines
    tio.c_cflag |= (CREAD | CLOCAL);

    // Baud rate
    speed_t bf = _baud_to_flag(c->baud ? c->baud : 115200);
    if (!bf) return -2;
    cfsetispeed(&tio, bf);
    cfsetospeed(&tio, bf);

    // Set non-blocking-like read behavior: we'll rely on poll()
    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tio) != 0) return -3;

    // Flush any stale data
    tcflush(fd, TCIOFLUSH);
    return 0;
}

HAL_Uart* HAL_Uart_Open(const HAL_UartConfig* cfg, HAL_UartStatus* out_status) {
    if (!cfg || !cfg->device) {
        if (out_status) *out_status = HAL_UART_EINVAL;
        return NULL;
    }

    int flags = O_RDWR | O_NOCTTY;          // read/write, ignore controlling TTY
    if (cfg->non_blocking) flags |= O_NONBLOCK;

    int fd = open(cfg->device, flags);
    if (fd < 0) {
        if (out_status) *out_status = HAL_UART_EIO;
        printf("[UART][LINUX] open %s failed errno=%d\r\n", cfg->device, errno);
        return NULL;
    }

    HAL_Uart* h = (HAL_Uart*)malloc(sizeof(HAL_Uart));
    if (!h) {
        close(fd);
        if (out_status) *out_status = HAL_UART_EIO;
        return NULL;
    }
    memset(h, 0, sizeof(*h));
    h->fd = fd;
    h->cfg = *cfg;

    if (_apply_cfg(fd, cfg) != 0) {
        if (out_status) *out_status = HAL_UART_ECFG;
        printf("[UART][LINUX] termios set failed\r\n");
        close(fd); free(h); return NULL;
    }

    if (out_status) *out_status = HAL_UART_OK;
    printf("[UART][LINUX] opened %s baud=%u\r\n", cfg->device, (unsigned)cfg->baud);
    return h;
}

void HAL_Uart_Close(HAL_Uart* h) {
    if (!h) return;
    if (h->fd >= 0) close(h->fd);
    free(h);
}

long HAL_Uart_Write(HAL_Uart* h, const void* buf, size_t len) {
    if (!h || h->fd < 0 || !buf) return -1;
    const uint8_t* p = (const uint8_t*)buf;
    size_t total = 0;
    while (total < len) {
        ssize_t n = write(h->fd, p + total, len - total);
        if (n < 0) {
            if (errno == EINTR) continue;  // interrupted by signal: retry
            return -2;                     // other write error
        }
        total += (size_t)n;
    }
    return (long)total;
}

long HAL_Uart_WriteString(HAL_Uart* h, const char* s) {
    if (!s) return -1;
    size_t len = strlen(s);
    return HAL_Uart_Write(h, s, len);
}

long HAL_Uart_Read(HAL_Uart* h, void* buf, size_t len, uint32_t timeout_ms) {
    if (!h || h->fd < 0 || !buf || len == 0) return -1;

    struct pollfd pfd;
    pfd.fd = h->fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    // -1 means "block forever" for poll()
    int to = (timeout_ms == 0xFFFFFFFFu) ? -1 : (int)timeout_ms;
    int rc = poll(&pfd, 1, to);
    if (rc < 0) {
        if (errno == EINTR) return 0; // interrupted: treat as timeout to keep tasks responsive
        return -2;
    }
    if (rc == 0) {
        // timeout
        return 0;
    }

    if (pfd.revents & POLLIN) {
        ssize_t n = read(h->fd, buf, len);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
            return -3;
        }
        return (long)n;
    }
    return 0;
}

HAL_UartStatus HAL_Uart_Flush(HAL_Uart* h, int which) {
    if (!h || h->fd < 0) return HAL_UART_EINVAL;
    int sel = TCIOFLUSH;
    if (which == 0) sel = TCIFLUSH;
    else if (which == 1) sel = TCOFLUSH;
    if (tcflush(h->fd, sel) != 0) return HAL_UART_EIO;
    return HAL_UART_OK;
}

int HAL_Uart_GetFd(HAL_Uart* h) {
    if (!h) return -1;
    return h->fd;
}
