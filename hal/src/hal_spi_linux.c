/**
 * @file hal_spi_linux.c
 * @brief Linux backend for HAL SPI using /dev/spidevX.Y
 *
 * Requires kernel spidev support and a device tree node that binds the SPI
 * controller chip-select to "spidev".
 *
 * We configure mode, bits-per-word, speed, etc. using SPI_IOC_* ioctls,
 * and perform transfers via SPI_IOC_MESSAGE(N).
 */

#include "hal_spi.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <linux/spi/spidev.h>  // struct spi_ioc_transfer, SPI_IOC_*

/* --- weak hooks: real build will use syscalls, test can override --- */
__attribute__((weak))
int hal_spi_port_open(const char* path, int flags)
{
    return open(path, flags);
}

__attribute__((weak))
int hal_spi_port_close(int fd)
{
    return close(fd);
}

__attribute__((weak))
int hal_spi_port_ioctl(int fd, unsigned long req, void* arg)
{
    return ioctl(fd, req, arg);
}

struct HAL_SpiBus {
    int      fd;
    char     dev_name[64];
    uint8_t  mode;
    uint8_t  bits_per_word;
    uint8_t  lsb_first;
    uint32_t speed_hz;
};

static int _starts_with(const char* s, const char* p) {
    if (!s || !p) return 0;
    while (*p) { if (*s++ != *p++) return 0; }
    return 1;
}

/* Helper to apply config via ioctl */
static HAL_SpiStatus _spi_apply_cfg(struct HAL_SpiBus* bus)
{
    uint8_t mode_ioctl = bus->mode & 0x3;
    if (bus->lsb_first)
        mode_ioctl |= SPI_LSB_FIRST;

    if (hal_spi_port_ioctl(bus->fd, SPI_IOC_WR_MODE, &mode_ioctl) < 0) {
        printf("[SPI][LINUX] set MODE fail errno=%d\r\n", errno);
        return HAL_SPI_EBUS;
    }
    if (hal_spi_port_ioctl(bus->fd, SPI_IOC_WR_BITS_PER_WORD, &bus->bits_per_word) < 0) {
        printf("[SPI][LINUX] set BPW fail errno=%d\r\n", errno);
        return HAL_SPI_EBUS;
    }
    if (hal_spi_port_ioctl(bus->fd, SPI_IOC_WR_MAX_SPEED_HZ, &bus->speed_hz) < 0) {
        printf("[SPI][LINUX] set SPEED fail errno=%d\r\n", errno);
        return HAL_SPI_EBUS;
    }
    return HAL_SPI_OK;
}

HAL_SpiBus* HAL_Spi_Open(const HAL_SpiConfig* cfg, HAL_SpiStatus* out_status)
{
    if (!cfg || !cfg->dev_name || !cfg->dev_name[0]) {
        if (out_status) *out_status = HAL_SPI_EINVAL;
        return NULL;
    }

    HAL_SpiBus* bus = (HAL_SpiBus*)calloc(1, sizeof(*bus));
    if (!bus) {
        if (out_status) *out_status = HAL_SPI_EBUS;
        return NULL;
    }

    int fd = hal_spi_port_open(cfg->dev_name, O_RDWR);
    if (fd < 0) {
        printf("[SPI][LINUX] open %s failed errno=%d\r\n", cfg->dev_name, errno);
        free(bus);
        if (out_status) *out_status = HAL_SPI_EBUS;
        return NULL;
    }

    bus->fd            = fd;
    strncpy(bus->dev_name, cfg->dev_name, sizeof(bus->dev_name)-1);
    bus->mode          = (uint8_t)cfg->mode;
    bus->bits_per_word = cfg->bits_per_word ? cfg->bits_per_word : 8;
    bus->lsb_first     = cfg->lsb_first;
    bus->speed_hz      = cfg->max_speed_hz ? cfg->max_speed_hz : 1000000;

    HAL_SpiStatus st = _spi_apply_cfg(bus);
    if (st != HAL_SPI_OK) {
        hal_spi_port_close(fd);
        free(bus);
        if (out_status) *out_status = st;
        return NULL;
    }

    if (out_status) *out_status = HAL_SPI_OK;
    return bus;
}

void HAL_Spi_Close(HAL_SpiBus* bus)
{
    if (!bus) return;
    if (bus->fd >= 0) {
        hal_spi_port_close(bus->fd);
    }
    free(bus);
}

/* ---------------------------------
 * HAL_Spi_Transfer
 * ---------------------------------
 * Full-duplex len bytes. If tx==NULL we send 0xFF.
 * If rx==NULL we discard reads.
 */
HAL_SpiStatus HAL_Spi_Transfer(HAL_SpiBus* bus,
                               const uint8_t* tx,
                               uint8_t*       rx,
                               size_t         len)
{
    if (!bus) return HAL_SPI_EINVAL;
    if (len == 0) return HAL_SPI_EINVAL;

    uint8_t* tx_buf_alloc = NULL;
    const uint8_t* tx_ptr = tx;
    if (!tx_ptr) {
        tx_buf_alloc = (uint8_t*)malloc(len);
        if (!tx_buf_alloc) return HAL_SPI_EBUS;
        memset(tx_buf_alloc, 0xFF, len);
        tx_ptr = tx_buf_alloc;
    }

    struct spi_ioc_transfer xfer;
    memset(&xfer, 0, sizeof(xfer));
    xfer.tx_buf        = (unsigned long)tx_ptr;
    xfer.rx_buf        = (unsigned long)rx;
    xfer.len           = len;
    xfer.speed_hz      = bus->speed_hz;
    xfer.bits_per_word = bus->bits_per_word;

    int ret = hal_spi_port_ioctl(bus->fd, SPI_IOC_MESSAGE(1), &xfer);
    if (tx_buf_alloc) free(tx_buf_alloc);

    if (ret < 0) {
        printf("[SPI][LINUX] Transfer fail errno=%d\r\n", errno);
        return HAL_SPI_EIO;
    }

    return HAL_SPI_OK;
}

/* ---------------------------------
 * HAL_Spi_TransferSegments
 * ---------------------------------
 * Phase A: send tx0 (len0 bytes), ignore RX
 * Phase B: send tx1 (len1 bytes) while capturing RX bytes into rx
 * Note:
 *   rx_len must == len1 or smaller/equal pattern. We'll capture min(len1, rx_len).
 */
HAL_SpiStatus HAL_Spi_TransferSegments(HAL_SpiBus* bus,
                                       const uint8_t* tx0, size_t len0,
                                       const uint8_t* tx1, size_t len1,
                                       uint8_t*       rx,
                                       size_t         rx_len)
{
    if (!bus) return HAL_SPI_EINVAL;

    // handle NULL tx1 if len1>0 -> fill dummy 0xFF
    uint8_t* tx1_alloc = NULL;
    const uint8_t* tx1_ptr = tx1;
    if (len1 > 0 && !tx1_ptr) {
        tx1_alloc = (uint8_t*)malloc(len1);
        if (!tx1_alloc) return HAL_SPI_EBUS;
        memset(tx1_alloc, 0xFF, len1);
        tx1_ptr = tx1_alloc;
    }

    // Prepare two transfers
    struct spi_ioc_transfer xfers[2];
    memset(xfers, 0, sizeof(xfers));

    // Phase 0: command / address phase
    if (tx0 && len0) {
        xfers[0].tx_buf        = (unsigned long)tx0;
        xfers[0].rx_buf        = 0;          // ignore
        xfers[0].len           = len0;
        xfers[0].speed_hz      = bus->speed_hz;
        xfers[0].bits_per_word = bus->bits_per_word;
        xfers[0].cs_change     = 0;          // keep CS asserted for next xfer
    }

    // Phase 1: data phase
    if (len1) {
        xfers[1].tx_buf        = (unsigned long)tx1_ptr;
        xfers[1].rx_buf        = (unsigned long)rx;
        xfers[1].len           = len1;
        xfers[1].speed_hz      = bus->speed_hz;
        xfers[1].bits_per_word = bus->bits_per_word;
        xfers[1].cs_change     = 0;          // release CS after this xfer
    }

    int nxfers = (tx0 && len0) ? ((len1>0)?2:1) : ((len1>0)?1:0);

    int ret = hal_spi_port_ioctl(bus->fd, SPI_IOC_MESSAGE(nxfers), xfers);
    if (tx1_alloc) free(tx1_alloc);

    if (ret < 0) {
        printf("[SPI][LINUX] Segments fail errno=%d\r\n", errno);
        return HAL_SPI_EIO;
    }

    // NOTE: If rx_len < len1, caller only cares about first rx_len bytes.
    // We already wrote directly into rx, so caller can just ignore extra.

    return HAL_SPI_OK;
}

/* ---------------------------------
 * HAL_Spi_SetSpeed / HAL_Spi_GetInfo
 * --------------------------------- */

HAL_SpiStatus HAL_Spi_SetSpeed(HAL_SpiBus* bus, uint32_t hz)
{
    if (!bus) return HAL_SPI_EINVAL;
    bus->speed_hz = hz;
    if (hal_spi_port_ioctl(bus->fd, SPI_IOC_WR_MAX_SPEED_HZ, &bus->speed_hz) < 0) {
        printf("[SPI][LINUX] SetSpeed fail errno=%d\r\n", errno);
        return HAL_SPI_EBUS;
    }
    return HAL_SPI_OK;
}

HAL_SpiStatus HAL_Spi_GetInfo(HAL_SpiBus* bus, HAL_SpiInfo* out)
{
    if (!bus || !out) {
        return HAL_SPI_EINVAL;
    }

    // start with what we already know in our struct
    memset(out, 0, sizeof(*out));
    strncpy(out->name, bus->dev_name, sizeof(out->name) - 1);
    out->mode          = bus->mode;
    out->bits_per_word = bus->bits_per_word;
    out->lsb_first     = bus->lsb_first;
    out->max_speed_hz  = bus->speed_hz;

    // optional: try to read back from driver to get the latest values
    uint8_t  mode_rd;
    uint8_t  bpw_rd;
    uint32_t speed_rd;

    if (hal_spi_port_ioctl(bus->fd, SPI_IOC_RD_MODE, &mode_rd) == 0) {
        out->mode = mode_rd & 0x3;
        out->lsb_first = (mode_rd & SPI_LSB_FIRST) ? 1 : 0;
    }

    if (hal_spi_port_ioctl(bus->fd, SPI_IOC_RD_BITS_PER_WORD, &bpw_rd) == 0) {
        out->bits_per_word = bpw_rd;
    }

    if (hal_spi_port_ioctl(bus->fd, SPI_IOC_RD_MAX_SPEED_HZ, &speed_rd) == 0) {
        out->max_speed_hz = speed_rd;
    }

    return HAL_SPI_OK;
}

/* ------------- convenience: write-only ------------- */
HAL_SpiStatus HAL_Spi_Write(HAL_SpiBus*    bus,
                            const uint8_t* tx,
                            size_t         len)
{
    // write-only == full-duplex with rx ignored
    return HAL_Spi_Transfer(bus, tx, NULL, len);
}

/* ------------- convenience: read-only ------------- */
HAL_SpiStatus HAL_Spi_Read(HAL_SpiBus*  bus,
                           uint8_t*     rx,
                           size_t       len)
{
    // read-only == full-duplex with tx dummy
    return HAL_Spi_Transfer(bus, NULL, rx, len);
}

/* ------------- burst transfer with cs_hold ------------- */
HAL_SpiStatus HAL_Spi_BurstTransfer(HAL_SpiBus*    bus,
                                    const uint8_t* tx,
                                    uint8_t*       rx,
                                    size_t         len,
                                    int            cs_hold)
{
    if (!bus || len == 0) return HAL_SPI_EINVAL;

    uint8_t* tx_buf_alloc = NULL;
    const uint8_t* tx_ptr = tx;
    if (!tx_ptr) {
        tx_buf_alloc = (uint8_t*)malloc(len);
        if (!tx_buf_alloc) return HAL_SPI_EBUS;
        memset(tx_buf_alloc, 0xFF, len);
        tx_ptr = tx_buf_alloc;
    }

    struct spi_ioc_transfer xfer;
    memset(&xfer, 0, sizeof(xfer));
    xfer.tx_buf        = (unsigned long)tx_ptr;
    xfer.rx_buf        = (unsigned long)rx;
    xfer.len           = len;
    xfer.speed_hz      = bus->speed_hz;
    xfer.bits_per_word = bus->bits_per_word;

    /*
     * cs_change semantics in spidev:
     *  - 0 : deassert CS after this transfer
     *  - 1 : keep CS asserted after this transfer
     *
     * So if caller wants to "hold CS low" across this message so the
     * *next* message is effectively continuous, we set cs_change=1.
     */
    xfer.cs_change     = cs_hold ? 1 : 0;

    int ret = hal_spi_port_ioctl(bus->fd, SPI_IOC_MESSAGE(2), &xfer);
    if (tx_buf_alloc) free(tx_buf_alloc);

    if (ret < 0) {
        printf("[SPI][LINUX] BurstTransfer fail errno=%d\r\n", errno);
        return HAL_SPI_EIO;
    }

    return HAL_SPI_OK;
}

/* ------------- manual CS control (stub) ------------- */
HAL_SpiStatus HAL_Spi_AssertCS(HAL_SpiBus* bus, int assert_level)
{
    (void)bus;
    (void)assert_level;

    /*
     * On many boards, SPI chip select is automatically toggled
     * by the SPI controller driver. If your board instead routes
     * CS to a GPIO pin (NOT managed by the controller), you could
     * extend HAL_SpiBus to store that GPIO line and drive it here.
     *
     * For now, Linux spidev backend does nothing and returns OK.
     */
    return HAL_SPI_OK;
}