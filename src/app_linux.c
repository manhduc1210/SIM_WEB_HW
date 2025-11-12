/**
 * @file app_linux.c
 * @brief Main entry for OSAL Linux demo application.
 *
 * This file initializes OSAL on Linux, then starts demo modules.
 * You can enable/disable demos (e.g., LED blink, UART HAL) from here.
 */

#include "osal.h"
#include "osal_task.h"
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <stdarg.h>
#include "demo_gpio_hal.h"
#include "hal_i2c.h"
#include "hal_spi.h"

// === Forward declarations for demos ===
void DemoUart_Start(const char* dev, uint32_t baud, int nb);  // from demo_uart.c
void DemoUart_Stop(void);                                     // stop/cleanup
void DemoGpio_Start(const DemoGpioCfg* cfg);
void DemoI2cExpander_Start(HAL_I2cBus* bus, uint8_t addr7);
void Demo_Spi_Basic(void);

// === SIGINT handler (Ctrl+C) ===
static volatile sig_atomic_t g_stop_requested = 0;
static void on_sigint(int sig) { 
    (void)sig; 
    g_stop_requested = 1; 
}

int main(void) {
    printf("=== OSAL Linux Demo App (Ctrl+C to exit) ===\n");

    // Install Ctrl+C handler
    // signal(SIGINT, on_sigint);

    // 1) OSAL init
    OSAL_Config cfg = {
        .backend = OSAL_BACKEND_LINUX,
        .log     = printf,
        .platform_ctx = NULL
    };
    if (OSAL_Init(&cfg) != OSAL_OK) {
        printf("[ERROR] OSAL_Init failed!\n");
        return -1;
    }

    // 3. Start UART HAL demo
    //    - Device: "/dev/ttyPS1" (ZedBoard's second UART port)
    //    - Baud:   115200
    //    - Non-blocking open: 0 (blocking)
    // DemoUart_Start("/dev/ttyPS0", 115200, 0);

    // TODO: Fill offsets from your board (use `gpioinfo`)
    DemoGpioCfg gpio_cfg = {
        .chip_name = "gpiochip0",
        .led_offsets = { /* LSB..MSB */ 0,1,2,3,4,5,6,7 }, // ví dụ
        .led_count = 8,
        .btn0_offset = 8,   // ví dụ BTN0
        .btn1_offset = 9,   // ví dụ BTN1
        .leds_active_low = 0,
        .btns_active_low = 1,  // thường nút kéo-up: pressed=0
        .debounce_ms = 10
    };
    DemoGpio_Start(&gpio_cfg);

    // HAL_I2cStatus st;
    // HAL_I2cBusConfig bus_cfg = { 
    //     .bus_name = "/dev/i2c-0", 
    //     .bus_speed_hz = 100000 
    // };
    // HAL_I2cBus* bus = HAL_I2cBus_Open(&bus_cfg, &st);

    // if (!bus || st != HAL_I2C_OK) {
    //     printf("I2C open failed (%d)\n", st);
    //     return -1;
    // }

    // // --- Gọi scan ---
    // uint8_t found[16];
    // int n = HAL_I2cBus_Scan(bus, found, 16);

    // if (n == 0) {
    //     printf("[I2C SCAN] No devices found on %s\n", bus_cfg.bus_name);
    // } else {
    //     printf("[I2C SCAN] Found %d device(s):\n", n);
    //     for (int i = 0; i < n; ++i)
    //         printf("  - 0x%02X\n", found[i]);
    // }

    // HAL_I2cBus_Close(bus);

    // DemoI2cExpander_Start(bus, 0x20);  // typical MCP23008 addr
    // DemoI2cTemp_Start("/dev/i2c-0");

    // HAL_SpiStatus st;
    // HAL_SpiConfig spi_cfg = {
    //     .dev_name      = "/dev/spidev0.0", // MUST exist after DT fix
    //     .mode          = HAL_SPI_MODE0,
    //     .max_speed_hz  = 1000000,
    //     .bits_per_word = 8,
    //     .lsb_first     = 0
    // };

    // HAL_SpiBus* spi_bus = HAL_Spi_Open(&spi_cfg, &st);
    // if (!spi_bus || st != HAL_SPI_OK) {
    //     printf("[APP] SPI open failed (%d)\n", st);
    //     return -1;
    // }

    // DemoOledSpi_Start(spi_bus);
    
    // printf("=== OSAL Linux Demo App (SPI basic) ===\n");
    // Demo_Spi_Basic();

    // 4. Let OSAL tasks run indefinitely
    //    In Linux backend, tasks are POSIX threads. We can just sleep forever.
    // while (!g_stop_requested) {
    //     OSAL_TaskDelayMs(1000);
    // }
    for (;;) OSAL_TaskDelayMs(1000);
    // printf("\n[APP] Ctrl+C detected. Stopping...\n");
    // DemoUart_Stop();
    // printf("[APP] Exit.\n");

    return 0;
}
