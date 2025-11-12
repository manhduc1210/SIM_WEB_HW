/**
 * @file hal_i2c_linux.c
 * @brief Linux backend for HAL I2C using /dev/i2c-* and ioctl(I2C_SLAVE).
 *
 * This backend talks to real I2C hardware through the i2c-dev userspace
 * interface. It requires:
 *   - Kernel I2C controller for Zynq PS (or PL) enabled
 *   - i2c-dev driver enabled so /dev/i2c-<N> exists
 *
 * All functions here implement the portable HAL_I2c API so that
 * upper layers (demos, sensor drivers, app) don't depend on Linux directly.
 *
 * NOTE:
 *   - This backend currently supports only 7-bit I2C addresses.
 *   - Multi-segment "burst" operations are emulated as write() then read()
 *     on the same slave address (common pattern).
 */

#include "hal_i2c.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>

#include <linux/i2c-dev.h>   // I2C_SLAVE, etc.

/* ------------------------------
 * Internal HAL_I2cBus definition
 * ------------------------------ */

struct HAL_I2cBus {
    int      fd;             // file descriptor for /dev/i2c-X
    char     dev_name[64];   // "/dev/i2c-0" etc
    uint32_t speed_hz_hint;  // we keep this for info only
};

/* Helper: do ioctl to select which slave address we're talking to right now */
static HAL_I2cStatus _i2c_set_addr(struct HAL_I2cBus* bus, uint8_t addr7) {
    if (!bus) return HAL_I2C_EINVAL;
    if (ioctl(bus->fd, I2C_SLAVE, addr7) < 0) {
        printf("[I2C][LINUX] ioctl(I2C_SLAVE,0x%02X) failed errno=%d\r\n",
                 addr7, errno);
        return HAL_I2C_ENODEV; // often errno=ENODEV or EBUSY if NACK or locked
    }
    return HAL_I2C_OK;
}

/* ------------------------------
 * Bus open/close/info
 * ------------------------------ */

HAL_I2cBus* HAL_I2cBus_Open(const HAL_I2cBusConfig* cfg, HAL_I2cStatus* out_status)
{
    if (!cfg || !cfg->bus_name || !cfg->bus_name[0]) {
        if (out_status) *out_status = HAL_I2C_EINVAL;
        return NULL;
    }

    HAL_I2cBus* bus = (HAL_I2cBus*)calloc(1, sizeof(*bus));
    if (!bus) {
        if (out_status) *out_status = HAL_I2C_EBUS;
        return NULL;
    }

    int fd = open(cfg->bus_name, O_RDWR);
    if (fd < 0) {
        printf("[I2C][LINUX] open %s failed errno=%d\r\n", cfg->bus_name, errno);
        free(bus);
        if (out_status) *out_status = HAL_I2C_EBUS;
        return NULL;
    }

    bus->fd = fd;
    bus->speed_hz_hint = cfg->bus_speed_hz;
    strncpy(bus->dev_name, cfg->bus_name, sizeof(bus->dev_name)-1);

    printf("[I2C][LINUX] opened %s (speed hint %u Hz)\r\n",
             bus->dev_name, (unsigned)bus->speed_hz_hint);

    if (out_status) *out_status = HAL_I2C_OK;
    return bus;
}

void HAL_I2cBus_Close(HAL_I2cBus* bus)
{
    if (!bus) return;
    if (bus->fd >= 0) {
        close(bus->fd);
    }
    free(bus);
}

HAL_I2cStatus HAL_I2cBus_Info(HAL_I2cBus* bus, HAL_I2cBusInfo* out_info)
{
    if (!bus || !out_info) return HAL_I2C_EINVAL;
    memset(out_info, 0, sizeof(*out_info));

    // bus name
    strncpy(out_info->name, bus->dev_name, sizeof(out_info->name)-1);
    // bus speed hint only; we can't easily read actual bus clock from user space
    out_info->speed_hz = bus->speed_hz_hint;

    return HAL_I2C_OK;
}

/* ------------------------------
 * Probe
 * ------------------------------ */

/**
 * On Linux, there's no official "ping" ioctl for generic I2C.
 * The common trick is:
 *   - set slave address
 *   - do a zero-length write() or a 1-byte dummy read()
 *
 * If the slave NACKs the address, kernel usually returns -1 and sets errno=EREMOTEIO / ENXIO.
 *
 * NOTE: This is heuristic. Some devices might not like random reads.
 * For safe behavior: many people just attempt a write of 0 bytes.
 * Some kernels reject zero-length write. So we try a dummy read of 1 byte,
 * and treat EIO / ENXIO as "no device".
 */
HAL_I2cStatus HAL_I2c_Probe(HAL_I2cBus* bus, uint8_t addr7)
{
    if (!bus) return HAL_I2C_EINVAL;

    HAL_I2cStatus st = _i2c_set_addr(bus, addr7);
    if (st != HAL_I2C_OK) {
        // means can't select that address, assume device not present
        return HAL_I2C_ENODEV;
    }

    uint8_t dummy;
    ssize_t r = read(bus->fd, &dummy, 1);
    if (r == 1 || r == 0) {
        // device responded somehow (some devices allow direct read,
        // others might just return 0 bytes)
        return HAL_I2C_OK;
    } else {
        // read failed -> likely no device ACK
        return HAL_I2C_ENODEV;
    }
}

/* ------------------------------
 * Low-level Write / Read
 * ------------------------------ */

HAL_I2cStatus HAL_I2c_Write(HAL_I2cBus* bus,
                            uint8_t addr7,
                            const uint8_t* data_out,
                            size_t len)
{
    if (!bus || !data_out) return HAL_I2C_EINVAL;
    HAL_I2cStatus st = _i2c_set_addr(bus, addr7);
    if (st != HAL_I2C_OK) return st;

    ssize_t w = write(bus->fd, data_out, len);
    if ((size_t)w != len) {
        printf("[I2C][LINUX] Write addr=0x%02X len=%u failed (errno=%d wrote=%d)\r\n",
                 addr7, (unsigned)len, errno, (int)w);
        return HAL_I2C_EIO;
    }
    return HAL_I2C_OK;
}

HAL_I2cStatus HAL_I2c_Read(HAL_I2cBus* bus,
                           uint8_t addr7,
                           uint8_t* data_in,
                           size_t len)
{
    if (!bus || !data_in) return HAL_I2C_EINVAL;
    HAL_I2cStatus st = _i2c_set_addr(bus, addr7);
    if (st != HAL_I2C_OK) return st;

    ssize_t r = read(bus->fd, data_in, len);
    if ((size_t)r != len) {
        printf("[I2C][LINUX] Read addr=0x%02X len=%u failed (errno=%d read=%d)\r\n",
                 addr7, (unsigned)len, errno, (int)r);
        return HAL_I2C_EIO;
    }
    return HAL_I2C_OK;
}

/* ------------------------------
 * Register access (8-bit register address)
 * ------------------------------ */

HAL_I2cStatus HAL_I2c_WriteReg8(HAL_I2cBus* bus,
                                uint8_t addr7,
                                uint8_t reg,
                                const uint8_t* data_out,
                                size_t len)
{
    if (!bus || !data_out) return HAL_I2C_EINVAL;

    HAL_I2cStatus st = _i2c_set_addr(bus, addr7);
    if (st != HAL_I2C_OK) return st;

    // Build buffer: [reg][payload...]
    uint8_t buf[256];
    if (len + 1 > sizeof(buf)) {
        return HAL_I2C_EINVAL;
    }
    buf[0] = reg;
    memcpy(&buf[1], data_out, len);

    ssize_t w = write(bus->fd, buf, len + 1);
    if ((size_t)w != (len + 1)) {
        printf("[I2C][LINUX] WriteReg8 addr=0x%02X reg=0x%02X len=%u failed (errno=%d wrote=%d)\r\n",
                 addr7, reg, (unsigned)len, errno, (int)w);
        return HAL_I2C_EIO;
    }

    return HAL_I2C_OK;
}

HAL_I2cStatus HAL_I2c_ReadReg8(HAL_I2cBus* bus,
                               uint8_t addr7,
                               uint8_t reg,
                               uint8_t* data_in,
                               size_t len)
{
    if (!bus || !data_in) return HAL_I2C_EINVAL;

    HAL_I2cStatus st = _i2c_set_addr(bus, addr7);
    if (st != HAL_I2C_OK) return st;

    // 1) Write the register pointer (no stop condition in Linux userspace;
    //    Actually i2c-dev will send a STOP after write() and then a repeated
    //    START before read(), but for 99% sensors this is fine.)
    ssize_t w = write(bus->fd, &reg, 1);
    if (w != 1) {
        printf("[I2C][LINUX] ReadReg8(addr=0x%02X) set reg=0x%02X failed errno=%d wrote=%d\r\n",
                 addr7, reg, errno, (int)w);
        return HAL_I2C_EIO;
    }

    // 2) Read data bytes
    ssize_t r = read(bus->fd, data_in, len);
    if ((size_t)r != len) {
        printf("[I2C][LINUX] ReadReg8 addr=0x%02X reg=0x%02X len=%u readfail errno=%d read=%d\r\n",
                 addr7, reg, (unsigned)len, errno, (int)r);
        return HAL_I2C_EIO;
    }

    return HAL_I2C_OK;
}

/* ------------------------------
 * Register access (16-bit register address)
 * Used by EEPROMs / some sensors with 16-bit register map
 * ------------------------------ */

HAL_I2cStatus HAL_I2c_WriteReg16(HAL_I2cBus* bus,
                                 uint8_t addr7,
                                 uint16_t reg16,
                                 const uint8_t* data_out,
                                 size_t len)
{
    if (!bus || !data_out) return HAL_I2C_EINVAL;

    HAL_I2cStatus st = _i2c_set_addr(bus, addr7);
    if (st != HAL_I2C_OK) return st;

    // [reg_hi][reg_lo][payload...]
    uint8_t buf[256];
    if (len + 2 > sizeof(buf)) {
        return HAL_I2C_EINVAL;
    }
    buf[0] = (uint8_t)((reg16 >> 8) & 0xFF);
    buf[1] = (uint8_t)(reg16 & 0xFF);
    memcpy(&buf[2], data_out, len);

    ssize_t w = write(bus->fd, buf, len + 2);
    if ((size_t)w != (len + 2)) {
        printf("[I2C][LINUX] WriteReg16 addr=0x%02X reg16=0x%04X len=%u failed errno=%d wrote=%d\r\n",
                 addr7, reg16, (unsigned)len, errno, (int)w);
        return HAL_I2C_EIO;
    }

    return HAL_I2C_OK;
}

HAL_I2cStatus HAL_I2c_ReadReg16(HAL_I2cBus* bus,
                                uint8_t addr7,
                                uint16_t reg16,
                                uint8_t* data_in,
                                size_t len)
{
    if (!bus || !data_in) return HAL_I2C_EINVAL;

    HAL_I2cStatus st = _i2c_set_addr(bus, addr7);
    if (st != HAL_I2C_OK) return st;

    uint8_t addrbuf[2];
    addrbuf[0] = (uint8_t)((reg16 >> 8) & 0xFF);
    addrbuf[1] = (uint8_t)(reg16 & 0xFF);

    // write 16-bit register pointer
    ssize_t w = write(bus->fd, addrbuf, 2);
    if (w != 2) {
        printf("[I2C][LINUX] ReadReg16 set reg16=0x%04X failed errno=%d wrote=%d\r\n",
                 reg16, errno, (int)w);
        return HAL_I2C_EIO;
    }

    // read response
    ssize_t r = read(bus->fd, data_in, len);
    if ((size_t)r != len) {
        printf("[I2C][LINUX] ReadReg16 addr=0x%02X reg16=0x%04X len=%u readfail errno=%d read=%d\r\n",
                 addr7, reg16, (unsigned)len, errno, (int)r);
        return HAL_I2C_EIO;
    }

    return HAL_I2C_OK;
}

/* ------------------------------
 * BurstTransfer
 * ------------------------------
 *
 * This helper is for patterns like:
 *   - send N command bytes (tx_buf)
 *   - then read back M bytes (rx_buf)
 *
 * On Linux i2c-dev, we can't do a single atomic "repeated start with no stop"
 * for arbitrary sequences unless we use I2C_RDWR ioctl with multiple messages.
 *
 * For simplicity (and portability), we do:
 *   write(tx_buf)
 *   read(rx_buf)
 *
 * Many sensors will accept this fine.
 *
 * NOTE:
 *  If you later need strict repeated-start without stop, you can upgrade this
 *  function to use the I2C_RDWR ioctl with struct i2c_rdwr_ioctl_data and
 *  MSG flags. For now, we keep it simple.
 */

HAL_I2cStatus HAL_I2c_BurstTransfer(HAL_I2cBus* bus,
                                    uint8_t addr7,
                                    const uint8_t* tx_buf,
                                    size_t tx_len,
                                    uint8_t* rx_buf,
                                    size_t rx_len)
{
    if (!bus) return HAL_I2C_EINVAL;

    HAL_I2cStatus st = _i2c_set_addr(bus, addr7);
    if (st != HAL_I2C_OK) return st;

    if (tx_buf && tx_len > 0) {
        ssize_t w = write(bus->fd, tx_buf, tx_len);
        if ((size_t)w != tx_len) {
            printf("[I2C][LINUX] BurstTransfer write addr=0x%02X tx_len=%u failed errno=%d wrote=%d\r\n",
                     addr7, (unsigned)tx_len, errno, (int)w);
            return HAL_I2C_EIO;
        }
    }

    if (rx_buf && rx_len > 0) {
        ssize_t r = read(bus->fd, rx_buf, rx_len);
        if ((size_t)r != rx_len) {
            printf("[I2C][LINUX] BurstTransfer read addr=0x%02X rx_len=%u failed errno=%d read=%d\r\n",
                     addr7, (unsigned)rx_len, errno, (int)r);
            return HAL_I2C_EIO;
        }
    }

    return HAL_I2C_OK;
}

/* ------------------------------
 * Bus scan
 * ------------------------------
 *
 * We try addresses from 0x03..0x77, call HAL_I2c_Probe() for each,
 * and record successes into found_addrs[].
 *
 * NOTE: This is slow-ish because we do a per-address ioctl+read.
 */

int HAL_I2cBus_Scan(HAL_I2cBus* bus,
                    uint8_t* found_addrs,
                    int max_found)
{
    if (!bus || !found_addrs || max_found <= 0) return 0;

    int count = 0;
    for (uint8_t addr = 0x03; addr < 0x78; ++addr) {
        if (HAL_I2c_Probe(bus, addr) == HAL_I2C_OK) {
            if (count < max_found) {
                found_addrs[count] = addr;
            }
            count++;
        }
        if (count >= max_found) break;
    }

    return count;
}
