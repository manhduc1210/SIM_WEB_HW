/**
 * @file hal_spi.h
 * @brief Portable SPI HAL API (OS-agnostic).
 *
 * This abstraction lets higher-level code (demos / drivers / apps)
 * talk to SPI devices without caring about the underlying OS or driver.
 *
 * Core ideas:
 *  - HAL_SpiBus: handle to a single SPI controller+CS target (e.g. spidev0.0)
 *  - Full-duplex transfers (TX and RX simultaneously)
 *  - Multi-segment transfers under one CS assertion
 *  - Runtime speed / mode config (if backend supports it)
 */

#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------
 * Status codes
 * --------------------------------- */
typedef enum {
    HAL_SPI_OK = 0,
    HAL_SPI_EINVAL,   ///< invalid argument
    HAL_SPI_EBUS,     ///< open/config fail
    HAL_SPI_EIO       ///< transfer failed
} HAL_SpiStatus;

/* ---------------------------------
 * SPI mode (CPOL/CPHA combos)
 * Matches standard SPI modes:
 *  MODE0: CPOL=0, CPHA=0
 *  MODE1: CPOL=0, CPHA=1
 *  MODE2: CPOL=1, CPHA=0
 *  MODE3: CPOL=1, CPHA=1
 * --------------------------------- */
typedef enum {
    HAL_SPI_MODE0 = 0,
    HAL_SPI_MODE1 = 1,
    HAL_SPI_MODE2 = 2,
    HAL_SPI_MODE3 = 3
} HAL_SpiMode;

/* ---------------------------------
 * Bus configuration at open time.
 *
 * dev_name      : platform-specific name, e.g. "/dev/spidev0.0" on Linux
 * mode          : SPI mode (0..3)
 * max_speed_hz  : desired SCLK (e.g. 1000000 for 1MHz)
 * bits_per_word : usually 8 for most devices
 * lsb_first     : 0 = MSB-first (common), 1 = LSB-first
 * --------------------------------- */
typedef struct {
    const char*  dev_name;
    HAL_SpiMode  mode;
    uint32_t     max_speed_hz;
    uint8_t      bits_per_word;
    uint8_t      lsb_first;
} HAL_SpiConfig;

/* Opaque handle (backend defines the struct) */
typedef struct HAL_SpiBus HAL_SpiBus;

/* ---------------------------------
 * Optional debug/info struct
 * --------------------------------- */
typedef struct {
    char     name[32];        ///< device name (e.g. "spidev0.0")
    uint32_t speed_hz;        ///< current max speed
    uint8_t  mode;            ///< 0..3
    uint8_t  bits_per_word;   ///< e.g. 8
    uint8_t  lsb_first;       ///< 0=MSB first
    uint32_t max_speed_hz;
} HAL_SpiInfo;

/* ---------------------------------
 * Open / Close
 * --------------------------------- */

/**
 * @brief Open an SPI bus/device and configure mode, speed, bpw, bit order.
 *
 * Returns NULL on error. If out_status != NULL, that will carry the reason.
 * The returned HAL_SpiBus* must later be closed with HAL_Spi_Close().
 */
HAL_SpiBus* HAL_Spi_Open(const HAL_SpiConfig* cfg, HAL_SpiStatus* out_status);

/**
 * @brief Close (free) an SPI bus handle.
 */
void HAL_Spi_Close(HAL_SpiBus* bus);

/* ---------------------------------
 * Transfers
 * --------------------------------- */

/**
 * @brief Full-duplex SPI transfer of 'len' bytes.
 *
 * tx may be NULL => we send 0xFF (or 0x00 backend-defined) while reading.
 * rx may be NULL => we ignore read data.
 *
 * This is the standard "1-phase" SPI transaction.
 */
HAL_SpiStatus HAL_Spi_Transfer(HAL_SpiBus* bus,
                               const uint8_t* tx,
                               uint8_t*       rx,
                               size_t         len);

/**
 * @brief Multi-segment transfer under one chip-select assertion.
 *
 * Many SPI slaves expect:
 *   1) Send command bytes (tx0)
 *   2) Then clock out some dummy bytes (tx1) while capturing response (rx)
 *
 * Example: reading JEDEC ID from flash:
 *   tx0 = [0x9F], len0=1
 *   tx1 = [0x00,0x00,0x00], len1=3 (dummy clocks)
 *   rx_len=3 to capture ID
 *
 * NOTE:
 *   - rx buffer corresponds to the SECOND phase ("tx1").
 *   - rx_len must match len1 (number of clocked bytes after cmd).
 *   - If your slave is simple loopback you might not need this API.
 */
HAL_SpiStatus HAL_Spi_TransferSegments(HAL_SpiBus* bus,
                                       const uint8_t* tx0, size_t len0,
                                       const uint8_t* tx1, size_t len1,
                                       uint8_t*       rx,
                                       size_t         rx_len);

/* ---------------------------------
 * Runtime config / info
 * --------------------------------- */

/**
 * @brief Change the max speed on an already-open bus (if supported).
 */
HAL_SpiStatus HAL_Spi_SetSpeed(HAL_SpiBus* bus, uint32_t hz);

/**
 * @brief Query current SPI config (mode, speed, bpw, etc) for logging.
 */
HAL_SpiStatus HAL_Spi_GetInfo(HAL_SpiBus* bus, HAL_SpiInfo* out_info);
HAL_SpiStatus HAL_Spi_Write(HAL_SpiBus* bus,const uint8_t* tx, size_t len);
HAL_SpiStatus HAL_Spi_Read(HAL_SpiBus* bus, uint8_t* rx, size_t len);
HAL_SpiStatus HAL_Spi_BurstTransfer(HAL_SpiBus* bus, const uint8_t* tx, uint8_t* rx, size_t len, int cs_hold);


#ifdef __cplusplus
}
#endif
