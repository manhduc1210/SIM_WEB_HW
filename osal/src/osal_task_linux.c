// OSAL task backend for Linux (pthread + cooperative suspend/stop + RT prio)
// - Suspend/Resume: cooperative via condvar (có hiệu lực khi task gọi OSAL_TaskDelayMs / OSAL_TaskYield)
// - Stop/Delete   : cooperative stop (flag + join) => an toàn tài nguyên
// - Priority      : SCHED_FIFO nếu có CAP_SYS_NICE, fallback SCHED_OTHER

#include "osal_task.h"
#include "osal.h"

#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#ifndef OSAL_MAX_TASKS
#define OSAL_MAX_TASKS 8
#endif

#ifndef OSAL_TASK_NAME_MAX
#define OSAL_TASK_NAME_MAX 16
#endif

typedef struct LinuxTask {
    uint8_t           used;
    pthread_t         tid;
    pthread_mutex_t   mtx;
    pthread_cond_t    cv;
    volatile int      running;     // 1: đang chạy, 0: yêu cầu dừng (stop/delete)
    volatile int      suspended;   // 1: yêu cầu tạm dừng (cooperative)
    char              name[OSAL_TASK_NAME_MAX];
    uint8_t           prio_req;    // prio yêu cầu (0..255)
    OSAL_TaskEntry    entry;
    void*             arg;
} LinuxTask;

static LinuxTask g_tasks[OSAL_MAX_TASKS];

// TLS: trỏ về task hiện tại (để Delay/Yield xử lý suspend/stop)
static __thread LinuxTask* tls_task = NULL;

static inline int map_prio_ucos_to_linux(uint8_t p_uc)
{
    // Linux SCHED_FIFO priority range: 1..99 (1 lowest, 99 highest)
    // Map tuyến tính 0..255 -> 1..99 (0->1, 255->99)
    int p = 1 + (int)((p_uc * 98u) / 255u);
    if (p < 1)  p = 1;
    if (p > 99) p = 99;
    return p;
}

static int set_thread_rt_priority(pthread_t tid, uint8_t prio_uc)
{
    int policy = SCHED_FIFO;
    struct sched_param sp;
    memset(&sp, 0, sizeof(sp));
    sp.sched_priority = map_prio_ucos_to_linux(prio_uc);

    int rc = pthread_setschedparam(tid, policy, &sp);
    if (rc != 0) {
        // Thường EPERM nếu không có CAP_SYS_NICE → fallback SCHED_OTHER
        struct sched_param sp2; memset(&sp2, 0, sizeof(sp2));
        policy = SCHED_OTHER;
        rc = pthread_setschedparam(tid, policy, &sp2);
        if (rc != 0) {
            OSAL_LOG("[OSAL][Task] set prio failed (rc=%d, errno=%d)\r\n", rc, errno);
            return -1;
        }
        OSAL_LOG("[OSAL][Task] fallback SCHED_OTHER (no CAP_SYS_NICE?)\r\n");
        return 1; // fallback
    }
    OSAL_LOG("[OSAL][Task] SCHED_FIFO prio=%d ok\r\n", sp.sched_priority);
    return 0;
}

static void* task_trampoline(void* arg)
{
    LinuxTask* t = (LinuxTask*)arg;
    tls_task = t;

    // Đặt tên thread (best-effort, giới hạn 16 bytes)
#if defined(__linux__)
    if (t->name[0]) {
        pthread_setname_np(pthread_self(), t->name);
    }
#endif

    // Thiết lập ưu tiên (sau khi thread đã start)
    if (t->prio_req) {
        set_thread_rt_priority(pthread_self(), t->prio_req);
    }

    // Gọi entry người dùng – cooperative suspend/stop được “bắt” trong OSAL_TaskDelayMs / Yield
    t->entry(t->arg);

    // Khi entry trả về: đánh dấu kết thúc
    pthread_mutex_lock(&t->mtx);
    t->running = 0;
    pthread_mutex_unlock(&t->mtx);
    pthread_cond_broadcast(&t->cv);

    return NULL;
}

// ===== Helper quản lý slot =====
static LinuxTask* alloc_task_slot(void)
{
    for (int i = 0; i < OSAL_MAX_TASKS; ++i) {
        if (!g_tasks[i].used) {
            LinuxTask* t = &g_tasks[i];
            memset(t, 0, sizeof(*t));
            t->used = 1;
            pthread_mutex_init(&t->mtx, NULL);
            pthread_cond_init(&t->cv, NULL);
            t->running = 1;
            return t;
        }
    }
    return NULL;
}

static void free_task_slot(LinuxTask* t)
{
    if (!t) return;
    pthread_mutex_destroy(&t->mtx);
    pthread_cond_destroy(&t->cv);
    memset(t, 0, sizeof(*t));
}

// ===== API =====

OSAL_Status OSAL_TaskCreate(OSAL_TaskHandle* out, OSAL_TaskEntry entry, void* arg, const OSAL_TaskAttr* attr)
{
    if (!out || !entry) return OSAL_EINVAL;

    LinuxTask* t = alloc_task_slot();
    if (!t) return OSAL_EINIT;

    t->entry = entry;
    t->arg   = arg;
    t->suspended = 0;

    if (attr && attr->name) {
        strncpy(t->name, attr->name, sizeof(t->name)-1);
        t->name[sizeof(t->name)-1] = 0;
    }
    if (attr) {
        t->prio_req = attr->prio; // map khi set schedparam
    }

    pthread_attr_t a;
    pthread_attr_init(&a);

    // Stack size nếu có
    if (attr && attr->stack_size) {
        size_t ss = attr->stack_size;
        // Linux thường cần tối thiểu ~PTHREAD_STACK_MIN, nhưng attr->stack_size từ RTOS là bytes, OK.
        if (ss < 16384) ss = 16384; // guard tối thiểu
        pthread_attr_setstacksize(&a, ss);
    }

    // Detached? => Không. Ta join khi delete/stop để đồng bộ dọn tài nguyên.
    pthread_attr_setdetachstate(&a, PTHREAD_CREATE_JOINABLE);

    int rc = pthread_create(&t->tid, &a, task_trampoline, t);
    pthread_attr_destroy(&a);
    if (rc != 0) {
        OSAL_LOG("[OSAL][Task] pthread_create failed rc=%d errno=%d\r\n", rc, errno);
        free_task_slot(t);
        return OSAL_EINIT;
    }

    *out = (OSAL_TaskHandle)t;
    return OSAL_OK;
}

// Cooperative suspend: đặt cờ và để task “đỗ” trong OSAL_TaskDelayMs / Yield
OSAL_Status OSAL_TaskSuspend(OSAL_TaskHandle h)
{
    LinuxTask* t = (LinuxTask*)h;
    if (!t || !t->used) return OSAL_EINVAL;

    pthread_mutex_lock(&t->mtx);
    t->suspended = 1;
    pthread_mutex_unlock(&t->mtx);
    return OSAL_OK;
}

OSAL_Status OSAL_TaskResume(OSAL_TaskHandle h)
{
    LinuxTask* t = (LinuxTask*)h;
    if (!t || !t->used) return OSAL_EINVAL;

    pthread_mutex_lock(&t->mtx);
    t->suspended = 0;
    pthread_mutex_unlock(&t->mtx);
    pthread_cond_broadcast(&t->cv);
    return OSAL_OK;
}

// Cooperative delete/stop: yêu cầu dừng + join
OSAL_Status OSAL_TaskDelete(OSAL_TaskHandle h)
{
    LinuxTask* t = (LinuxTask*)h;
    if (!t || !t->used) return OSAL_EINVAL;

    // Báo dừng
    pthread_mutex_lock(&t->mtx);
    t->running = 0;
    t->suspended = 0;
    pthread_mutex_unlock(&t->mtx);
    pthread_cond_broadcast(&t->cv);

    // Chờ thread kết thúc
    (void)pthread_join(t->tid, NULL);
    free_task_slot(t);
    return OSAL_OK;
}

// Đổi priority runtime
OSAL_Status OSAL_TaskChangePrio(OSAL_TaskHandle h, uint8_t new_prio)
{
    LinuxTask* t = (LinuxTask*)h;
    if (!t || !t->used) return OSAL_EINVAL;

    int rc = set_thread_rt_priority(t->tid, new_prio);
    if (rc >= 0) {
        t->prio_req = new_prio;
        return OSAL_OK;
    }
    return OSAL_EINIT;
}

OSAL_Status OSAL_TaskGetState(OSAL_TaskHandle h, OSAL_TaskState* state)
{
    LinuxTask* t = (LinuxTask*)h;
    if (!t || !state) return OSAL_EINVAL;

    pthread_mutex_lock(&t->mtx);
    if (!t->running) {
        *state = OSAL_TASK_STATE_INVALID;  // hoặc TERMINATED nếu bạn có enum đó
    } else if (t->suspended) {
        *state = OSAL_TASK_STATE_WAITING;
    } else {
        *state = OSAL_TASK_STATE_RUNNING;
    }
    pthread_mutex_unlock(&t->mtx);
    return OSAL_OK;
}

OSAL_Status OSAL_TaskGetName(OSAL_TaskHandle h, const char** name)
{
    LinuxTask* t = (LinuxTask*)h;
    if (!t || !name) return OSAL_EINVAL;
    *name = t->name[0] ? t->name : NULL;
    return OSAL_OK;
}

// ===== Scheduling helpers (cooperative suspend/stop hook) =====

void OSAL_TaskYield(void)
{
    // Nếu task đang bị suspend → chờ đến khi resume
    if (tls_task) {
        LinuxTask* t = tls_task;
        pthread_mutex_lock(&t->mtx);
        while (t->running && t->suspended) {
            pthread_cond_wait(&t->cv, &t->mtx);
        }
        int still_running = t->running;
        pthread_mutex_unlock(&t->mtx);

        if (!still_running) {
            // Thoát trơn tru: return về entry → trampoline set running=0
            pthread_exit(NULL);
        }
    }
    sched_yield();
}

void OSAL_TaskDelayMs(uint32_t ms)
{
    // Chia nhỏ delay để kiểm tra suspend/stop mượt mà
    const uint32_t slice_ms = (ms > 50u) ? 10u : ms; // kiểm tra mỗi 10ms nếu delay dài
    uint32_t remain = ms;

    while (remain > 0) {
        uint32_t d = (remain > slice_ms) ? slice_ms : remain;
        struct timespec ts;
        ts.tv_sec  = d / 1000u;
        ts.tv_nsec = (long)(d % 1000u) * 1000000L;
        nanosleep(&ts, NULL);
        remain -= d;

        if (tls_task) {
            LinuxTask* t = tls_task;
            pthread_mutex_lock(&t->mtx);
            // Nếu suspend → chờ đến khi resume
            while (t->running && t->suspended) {
                pthread_cond_wait(&t->cv, &t->mtx);
            }
            int still_running = t->running;
            pthread_mutex_unlock(&t->mtx);
            if (!still_running) {
                pthread_exit(NULL);
            }
        }
    }
}

// ===== Optional: thống kê / duyệt =====

uint32_t OSAL_TaskCount(void)
{
    uint32_t n = 0;
    for (int i = 0; i < OSAL_MAX_TASKS; ++i)
        if (g_tasks[i].used) ++n;
    return n;
}

OSAL_Status OSAL_TaskForEach(void (*cb)(OSAL_TaskHandle h, void* arg), void* arg)
{
    if (!cb) return OSAL_EINVAL;
    for (int i = 0; i < OSAL_MAX_TASKS; ++i)
        if (g_tasks[i].used) cb((OSAL_TaskHandle)&g_tasks[i], arg);
    return OSAL_OK;
}
