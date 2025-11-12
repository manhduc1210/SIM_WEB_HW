#pragma once
#include <stdint.h>
#include <stddef.h>

typedef enum {
    OSAL_OK = 0,
    OSAL_EINVAL,
    OSAL_ETIMEOUT,
    OSAL_EOS,
    OSAL_EINIT,
} OSAL_Status;

typedef enum {
    OSAL_BACKEND_UCOS3 = 1,
    OSAL_BACKEND_FREERTOS,
    OSAL_BACKEND_LINUX
} OSAL_Backend;

typedef void (*OSAL_LogFn)(const char *fmt, ...);
