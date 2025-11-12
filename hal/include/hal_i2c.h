/**
 * @file hal_i2c.h
 * @brief Portable I2C HAL API (OS-agnostic).
 *
 * This header defines a generic I2C interface that higher-level code (apps,
 * drivers, demos) can use without caring about the OS / driver underneath.
 *
 * Backends:
 *  - hal_i2c_mock.c  : mock "virtual sensor", no real hardware (for dev/demo)
 *  - hal_i2c_linux.c : /dev/i2c-X + ioctl(I2C_SLAVE)   (for real Linux)
 *  - hal_i2c_<rtos>.c: future FreeRTOS / uC-OS / baremetal backends
 *
 * Core concepts:
 *  - HAL_I2cBus: represents one I2C controller bus
 *  - 7-bit slave address (addr7)
 *  - Read/Write raw bytes to that slave
 *  - Read/Write typical register-based sensors (8-bit or 16-bit register index)
 *  - Burst transactions (write-then-read)
 *  - Bus scan helper
 */

#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward-declared opaque bus handle.
 * The backend defines struct HAL_I2cBus internally. */
typedef struct HAL_I2cBus HAL_I2cBus;

/* Status / error codes for HAL I2C */
typedef enum {
    HAL_I2C_OK = 0,     ///< success / ACK
    HAL_I2C_EINVAL,     ///< invalid argument / bad pointer / length
    HAL_I2C_EIO,        ///< general I/O failure talking to device
    HAL_I2C_ENODEV,     ///< no such device / NACK
    HAL_I2C_EBUS        ///< bus open / config error
} HAL_I2cStatus;

/**
 * @brief Bus configuration when opening.
 *
 * In Linux backend:
 *   - bus_name is something like "/dev/i2c-0"
 *   - bus_speed_hz is a hint (we often cannot force it from userspace)
 *
 * In mock backend:
 *   - bus_name can be "mock-bus-0" (arbitrary string)
 */
typedef struct {
    const char* bus_name;
    uint32_t    bus_speed_hz;
} HAL_I2cBusConfig;

/**
 * @brief Open an I2C bus and return a handle.
 *
 * Returns NULL on failure. If out_status != NULL, it will hold the reason.
 * The returned HAL_I2cBus* must later be closed via HAL_I2cBus_Close().
 */
HAL_I2cBus* HAL_I2cBus_Open(const HAL_I2cBusConfig* cfg, HAL_I2cStatus* out_status);

/**
 * @brief Close the bus handle and free resources.
 */
void HAL_I2cBus_Close(HAL_I2cBus* bus);

/**
 * @brief Check if a 7-bit address responds (ACK).
 *
 * Typical usage: scanning the bus for attached devices.
 * Returns HAL_I2C_OK if device at addr7 looks present.
 */
HAL_I2cStatus HAL_I2c_Probe(HAL_I2cBus* bus, uint8_t addr7);

/**
 * @brief Low-level write: send 'len' bytes to slave at addr7.
 * No register index is sent first. This is often used for streaming devices
 * or displays.
 */
HAL_I2cStatus HAL_I2c_Write(HAL_I2cBus* bus,
                            uint8_t addr7,
                            const uint8_t* data_out,
                            size_t len);

/**
 * @brief Low-level read: read 'len' bytes from slave at addr7.
 * No register index is sent first.
 */
HAL_I2cStatus HAL_I2c_Read(HAL_I2cBus* bus,
                           uint8_t addr7,
                           uint8_t* data_in,
                           size_t len);

/**
 * @brief Write to an 8-bit register, typical for sensors.
 *
 * This performs a single transaction that sends:
 *   [ reg | data_out[0] | data_out[1] | ... ]
 */
HAL_I2cStatus HAL_I2c_WriteReg8(HAL_I2cBus* bus,
                                uint8_t addr7,
                                uint8_t reg,
                                const uint8_t* data_out,
                                size_t len);

/**
 * @brief Read from an 8-bit register, typical for sensors.
 *
 * This performs:
 *   write(reg) then read(len)
 */
HAL_I2cStatus HAL_I2c_ReadReg8(HAL_I2cBus* bus,
                               uint8_t addr7,
                               uint8_t reg,
                               uint8_t* data_in,
                               size_t len);

/**
 * @brief Convenience helpers for single-byte registers.
 */
static inline HAL_I2cStatus HAL_I2c_ReadReg8_U8(HAL_I2cBus* bus,
                                                uint8_t addr7,
                                                uint8_t reg,
                                                uint8_t* out_val)
{
    return HAL_I2c_ReadReg8(bus, addr7, reg, out_val, 1);
}

static inline HAL_I2cStatus HAL_I2c_WriteReg8_U8(HAL_I2cBus* bus,
                                                 uint8_t addr7,
                                                 uint8_t reg,
                                                 uint8_t val)
{
    return HAL_I2c_WriteReg8(bus, addr7, reg, &val, 1);
}

/* ======================
 * Extended helpers (completeness)
 * ====================== */

/**
 * @brief Write to a 16-bit register index.
 * Many eeproms / sensors use 16-bit register/offset.
 *
 * This constructs:
 *   [ reg_hi | reg_lo | data_out... ]
 */
HAL_I2cStatus HAL_I2c_WriteReg16(HAL_I2cBus* bus,
                                 uint8_t addr7,
                                 uint16_t reg16,
                                 const uint8_t* data_out,
                                 size_t len);

/**
 * @brief Read from a 16-bit register index.
 * This performs:
 *   write(reg_hi, reg_lo) then read(len)
 */
HAL_I2cStatus HAL_I2c_ReadReg16(HAL_I2cBus* bus,
                                uint8_t addr7,
                                uint16_t reg16,
                                uint8_t* data_in,
                                size_t len);

/**
 * @brief Burst "write-then-read" transaction in a single helper.
 * Useful for complex sensors where you send N command bytes,
 * then expect M response bytes.
 *
 * The backend should try to keep it as atomic as possible for that OS.
 * For mock we just emulate it; for Linux we would do a write() then read().
 */
HAL_I2cStatus HAL_I2c_BurstTransfer(HAL_I2cBus* bus,
                                    uint8_t addr7,
                                    const uint8_t* tx_buf,
                                    size_t tx_len,
                                    uint8_t* rx_buf,
                                    size_t rx_len);

/**
 * @brief Scan the bus for devices 0x03..0x77 and report hits.
 *
 * 'found_addrs' is an array of up to max_found entries. We'll fill it with
 * addresses that ACK. Returns number of found devices (0..max_found).
 */
int HAL_I2cBus_Scan(HAL_I2cBus* bus,
                    uint8_t* found_addrs,
                    int max_found);

/**
 * @brief Optional bus info helper.
 * Some backends can fill in details (speed, name, capabilities).
 * Others might just provide what they know.
 */
typedef struct {
    char     name[32];        ///< bus name / label
    uint32_t speed_hz;        ///< nominal speed if known
    uint8_t  reserved[32];    ///< room to extend without breaking ABI
} HAL_I2cBusInfo;

HAL_I2cStatus HAL_I2cBus_Info(HAL_I2cBus* bus, HAL_I2cBusInfo* out_info);

#ifdef __cplusplus
}
#endif
