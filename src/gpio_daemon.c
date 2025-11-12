/*
 * gpio_daemon.c
 * Daemon mô phỏng demo BTN0 -> +1, BTN1 -> reset
 * Dựa trên logic của demo_gpio_hal.c nhưng chạy trong 1 process Linux,
 * và mở UNIX socket để nhận lệnh từ Python/gRPC.
 *
 * Lệnh qua socket (text):
 *   "PRESS 0\n"   -> giả lập nhấn BTN0
 *   "PRESS 1\n"   -> giả lập nhấn BTN1
 *   "RELEASE 0\n" -> giả lập thả BTN0
 *   "RELEASE 1\n" -> giả lập thả BTN1
 *   "GETLED\n"    -> trả về "LED a b c d\n"
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <sys/select.h>

#include "hal_gpio.h"

/* các hàm SIM phía C mà ta cần để giả lập nút và đọc LED */
HAL_GpioStatus HAL_GpioSim_SetInput(HAL_GpioChip* chip, int offset, int logic_val);
HAL_GpioStatus HAL_GpioSim_GetOutput(HAL_GpioChip* chip, int offset, int* out_logic);

#define SOCK_PATH "/tmp/gpio_sim.sock"

/* ====== phần giống demo_gpio_hal.c ====== */

/* cấu hình demo (giống DemoGpioCfg trong demo_gpio_hal.c) */
typedef struct {
    const char* chip_name;
    int         led_count;
    int         led_offsets[8];
    int         leds_active_low;
    int         btn0_offset;
    int         btn1_offset;
    int         btns_active_low;
    int         debounce_ms;
} DemoCfg;

/* biến global như trong demo_gpio_hal.c */
static HAL_GpioChip*   s_chip    = NULL;
static HAL_GpioLine*   s_leds[8] = {0};
static int             s_led_n   = 0;
static HAL_GpioLine*   s_btn0    = NULL;
static HAL_GpioLine*   s_btn1    = NULL;
static unsigned        s_count   = 0;  /* 0..255 */
static int             s_run     = 1;

/* hiển thị giá trị 8 bit ra dãy LED */
static void leds_show8(unsigned val)
{
    for (int i = 0; i < s_led_n; ++i) {
        if (s_leds[i]) {
            HAL_GpioLine_Write(s_leds[i], (val >> i) & 1u);
        }
    }
}

/* init GPIO demo: mở chip, request LED, request BTN0/BTN1 */
static int demo_init(const DemoCfg* cfg)
{
    HAL_GpioChipConfig cc = { .chip_name = cfg->chip_name };
    if (HAL_GpioChip_Open(&cc, &s_chip) != HAL_GPIO_OK) {
        fprintf(stderr, "[DAEMON] open chip fail\n");
        return -1;
    }

    /* request LED lines */
    s_led_n = cfg->led_count;
    for (int i = 0; i < s_led_n; ++i) {
        HAL_GpioLineConfig lc = {
            .offset  = cfg->led_offsets[i],
            .name    = NULL,
            .dir     = HAL_GPIO_DIR_OUT,
            .active  = cfg->leds_active_low ? HAL_GPIO_ACTIVE_LOW : HAL_GPIO_ACTIVE_HIGH,
            .drive   = HAL_GPIO_DRIVE_PUSHPULL,
            .bias    = HAL_GPIO_BIAS_AS_IS,
            .initial = 0,
            .edge    = HAL_GPIO_EDGE_NONE,
            .debounce_ms = 0
        };
        if (HAL_GpioLine_Request(s_chip, &lc, &s_leds[i]) != HAL_GPIO_OK) {
            fprintf(stderr, "[DAEMON] LED %d request failed\n", i);
            return -2;
        }
    }

    /* request BTN0/BTN1 */
    HAL_GpioLineConfig bc = {
        .offset  = cfg->btn0_offset,
        .name    = NULL,
        .dir     = HAL_GPIO_DIR_IN,
        .active  = cfg->btns_active_low ? HAL_GPIO_ACTIVE_LOW : HAL_GPIO_ACTIVE_HIGH,
        .drive   = HAL_GPIO_DRIVE_PUSHPULL,
        .bias    = HAL_GPIO_BIAS_AS_IS,
        .initial = 0,
        .edge    = HAL_GPIO_EDGE_NONE,
        .debounce_ms = cfg->debounce_ms
    };
    if (HAL_GpioLine_Request(s_chip, &bc, &s_btn0) != HAL_GPIO_OK) {
        fprintf(stderr, "[DAEMON] BTN0 request failed\n");
        return -3;
    }
    bc.offset = cfg->btn1_offset;
    if (HAL_GpioLine_Request(s_chip, &bc, &s_btn1) != HAL_GPIO_OK) {
        fprintf(stderr, "[DAEMON] BTN1 request failed\n");
        return -4;
    }

    s_count = 0;
    leds_show8(s_count);
    return 0;
}

/* ====== socket setup ====== */

static int setup_socket(void)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCK_PATH, sizeof(addr.sun_path)-1);

    unlink(SOCK_PATH);  /* xoá cũ nếu có */

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(fd);
        return -1;
    }

    if (listen(fd, 1) < 0) {
        perror("listen");
        close(fd);
        return -1;
    }

    printf("[DAEMON] listening on %s\n", SOCK_PATH);
    return fd;
}

/* xử lý 1 dòng lệnh từ client */
static void handle_cmd(const char* buf, int cfd, const DemoCfg* cfg)
{
    if (strncmp(buf, "PRESS", 5) == 0) {
        int idx = atoi(buf + 6);
        int offset = (idx == 0) ? cfg->btn0_offset : cfg->btn1_offset;
        HAL_GpioSim_SetInput(s_chip, offset, 1);
        write(cfd, "OK\n", 3);
    } else if (strncmp(buf, "RELEASE", 7) == 0) {
        int idx = atoi(buf + 8);
        int offset = (idx == 0) ? cfg->btn0_offset : cfg->btn1_offset;
        HAL_GpioSim_SetInput(s_chip, offset, 0);
        write(cfd, "OK\n", 3);
    } else if (strncmp(buf, "GETLED", 6) == 0) {
        int v[4] = {0};
        for (int i = 0; i < cfg->led_count; ++i) {
            int tmp = 0;
            HAL_GpioSim_GetOutput(s_chip, cfg->led_offsets[i], &tmp);
            v[i] = tmp;
        }
        char out[128];
        int len = snprintf(out, sizeof(out), "LED %d %d %d %d\n", v[0], v[1], v[2], v[3]);
        write(cfd, out, len);
    } else {
        write(cfd, "ERR\n", 4);
    }
}

int main(void)
{
    /* cấu hình mô phỏng giống bạn đang làm */
    DemoCfg cfg = {
        .chip_name       = "sim-gpio",
        .led_count       = 4,
        .led_offsets     = {0,1,2,3,0,0,0,0},
        .leds_active_low = 0,
        .btn0_offset     = 12,
        .btn1_offset     = 13,
        .btns_active_low = 0,
        .debounce_ms     = 5
    };

    if (demo_init(&cfg) != 0) {
        return 1;
    }
    printf("[DAEMON] demo gpio init ok\n");

    int lfd = setup_socket();
    if (lfd < 0) return 1;

    int cfd = accept(lfd, NULL, NULL);
    if (cfd < 0) {
        perror("accept");
        return 1;
    }
    printf("[DAEMON] client connected\n");

    /* ====== vòng lặp giống demo_gpio_hal.c ====== */

    const int step_ms     = 5;
    const int debounce_ms = (cfg.debounce_ms > 0) ? cfg.debounce_ms : 5;

    int last0 = 0, last1 = 0;
    int stable0 = 0, stable1 = 0;
    int acc0 = 0, acc1 = 0;
    int prev0 = 0, prev1 = 0;

    leds_show8(s_count);

    while (s_run) {
        /* 1) đọc BTN giống demo */
        int v0 = 0, v1 = 0;
        HAL_GpioLine_Read(s_btn0, &v0);
        HAL_GpioLine_Read(s_btn1, &v1);

        /* debounce BTN0 */
        if (v0 == last0) { acc0 += step_ms; if (acc0 >= debounce_ms) stable0 = v0; }
        else             { acc0 = 0; }
        /* debounce BTN1 */
        if (v1 == last1) { acc1 += step_ms; if (acc1 >= debounce_ms) stable1 = v1; }
        else             { acc1 = 0; }

        /* rising edge detect */
        int rising0 = (stable0 && !prev0);
        int rising1 = (stable1 && !prev1);

        if (rising0) {
            if (s_count < 255) s_count++;
            printf("[DAEMON][BTN0] ++ -> %u\n", s_count);
            leds_show8(s_count);
        }
        if (rising1) {
            s_count = 0;
            printf("[DAEMON][BTN1] reset -> %u\n", s_count);
            leds_show8(s_count);
        }

        prev0 = stable0;
        prev1 = stable1;
        last0 = v0;
        last1 = v1;

        /* 2) xử lý lệnh từ Python nếu có (non-blocking) */
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(cfd, &rfds);
        struct timeval tv = {0, 0};
        int rv = select(cfd + 1, &rfds, NULL, NULL, &tv);
        if (rv > 0 && FD_ISSET(cfd, &rfds)) {
            char buf[128];
            ssize_t n = read(cfd, buf, sizeof(buf)-1);
            if (n > 0) {
                buf[n] = '\0';
                handle_cmd(buf, cfd, &cfg);
            }
        }

        /* 3) delay giống OSAL_TaskDelayMs(5) */
        usleep(step_ms * 1000);
    }

    /* cleanup (nếu cần) */
    close(cfd);
    close(lfd);
    unlink(SOCK_PATH);

    for (int i = 0; i < s_led_n; ++i) {
        if (s_leds[i]) HAL_GpioLine_Release(s_leds[i]);
    }
    if (s_btn0) HAL_GpioLine_Release(s_btn0);
    if (s_btn1) HAL_GpioLine_Release(s_btn1);
    if (s_chip) HAL_GpioChip_Close(s_chip);

    return 0;
}
