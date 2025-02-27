#include "timer.h"
#include "thread.h"
#include "check.h"
#include "memmgr.h"
#include "kernel.h"

#include <FreeRTOS.h>
#include <timers.h>

const char* current_timer_name = NULL;

typedef struct {
    FuriTimerCallback func;
    void* context;
} TimerCallback_t;

const char* furi_timer_get_current_name() {
    return current_timer_name;
}

static void TimerCallback(TimerHandle_t hTimer) {
    TimerCallback_t* callb;

    /* Retrieve pointer to callback function and context */
    callb = (TimerCallback_t*)pvTimerGetTimerID(hTimer);

    /* Remove dynamic allocation flag */
    callb = (TimerCallback_t*)((uint32_t)callb & ~1U);

    if(callb != NULL) {
        current_timer_name = pcTimerGetName(hTimer);
        callb->func(callb->context);
        current_timer_name = NULL;
    }
}

FuriTimer* furi_timer_alloc(FuriTimerCallback func, FuriTimerType type, void* context) {
    furi_check((furi_kernel_is_irq_or_masked() == 0U) && (func != NULL));

    TimerHandle_t hTimer;
    TimerCallback_t* callb;
    UBaseType_t reload;

    hTimer = NULL;

    /* Dynamic memory allocation is available: if memory for callback and */
    /* its context is not provided, allocate it from dynamic memory pool */
    callb = (TimerCallback_t*)malloc(sizeof(TimerCallback_t));

    callb->func = func;
    callb->context = context;

    if(type == FuriTimerTypeOnce) {
        reload = pdFALSE;
    } else {
        reload = pdTRUE;
    }

    // Timer name so thread appid works in timers, and so does APP_DATA_PATH()
    const char* name = furi_thread_get_appid(furi_thread_get_current_id());

    /* Store callback memory dynamic allocation flag */
    callb = (TimerCallback_t*)((uint32_t)callb | 1U);
    // TimerCallback function is always provided as a callback and is used to call application
    // specified function with its context both stored in structure callb.
    hTimer = xTimerCreate(name, portMAX_DELAY, reload, callb, TimerCallback);
    furi_check(hTimer);

    /* Return timer ID */
    return ((FuriTimer*)hTimer);
}

void furi_timer_free(FuriTimer* instance) {
    furi_check(!furi_kernel_is_irq_or_masked());
    furi_check(instance);

    TimerHandle_t hTimer = (TimerHandle_t)instance;
    TimerCallback_t* callb;

    callb = (TimerCallback_t*)pvTimerGetTimerID(hTimer);

    if((uint32_t)callb & 1U) {
        /* If callback memory was allocated, it is only safe to free it with
         * the timer inactive. Send a stop command and wait for the timer to
         * be in an inactive state.
         */
        furi_check(xTimerStop(hTimer, portMAX_DELAY) == pdPASS);
        while(furi_timer_is_running(instance)) furi_delay_tick(2);

        /* Callback memory was allocated from dynamic pool, clear flag */
        callb = (TimerCallback_t*)((uint32_t)callb & ~1U);

        /* Return allocated memory to dynamic pool */
        free(callb);
    }

    furi_check(xTimerDelete(hTimer, portMAX_DELAY) == pdPASS);
}

FuriStatus furi_timer_start(FuriTimer* instance, uint32_t ticks) {
    furi_check(!furi_kernel_is_irq_or_masked());
    furi_check(instance);
    furi_check(ticks < portMAX_DELAY);

    TimerHandle_t hTimer = (TimerHandle_t)instance;
    FuriStatus stat;

    if(xTimerChangePeriod(hTimer, ticks, portMAX_DELAY) == pdPASS) {
        stat = FuriStatusOk;
    } else {
        stat = FuriStatusErrorResource;
    }

    /* Return execution status */
    return (stat);
}

FuriStatus furi_timer_restart(FuriTimer* instance, uint32_t ticks) {
    furi_check(!furi_kernel_is_irq_or_masked());
    furi_check(instance);
    furi_check(ticks < portMAX_DELAY);

    TimerHandle_t hTimer = (TimerHandle_t)instance;
    FuriStatus stat;

    if(xTimerChangePeriod(hTimer, ticks, portMAX_DELAY) == pdPASS &&
       xTimerReset(hTimer, portMAX_DELAY) == pdPASS) {
        stat = FuriStatusOk;
    } else {
        stat = FuriStatusErrorResource;
    }

    /* Return execution status */
    return (stat);
}

FuriStatus furi_timer_stop(FuriTimer* instance) {
    furi_check(!furi_kernel_is_irq_or_masked());
    furi_check(instance);

    TimerHandle_t hTimer = (TimerHandle_t)instance;

    furi_check(xTimerStop(hTimer, portMAX_DELAY) == pdPASS);

    return FuriStatusOk;
}

uint32_t furi_timer_is_running(FuriTimer* instance) {
    furi_check(!furi_kernel_is_irq_or_masked());
    furi_check(instance);

    TimerHandle_t hTimer = (TimerHandle_t)instance;

    /* Return 0: not running, 1: running */
    return (uint32_t)xTimerIsTimerActive(hTimer);
}

uint32_t furi_timer_get_expire_time(FuriTimer* instance) {
    furi_check(!furi_kernel_is_irq_or_masked());
    furi_check(instance);

    TimerHandle_t hTimer = (TimerHandle_t)instance;

    return (uint32_t)xTimerGetExpiryTime(hTimer);
}

void furi_timer_pending_callback(FuriTimerPendigCallback callback, void* context, uint32_t arg) {
    furi_check(callback);

    BaseType_t ret = pdFAIL;
    if(furi_kernel_is_irq_or_masked()) {
        ret = xTimerPendFunctionCallFromISR(callback, context, arg, NULL);
    } else {
        ret = xTimerPendFunctionCall(callback, context, arg, FuriWaitForever);
    }

    furi_check(ret == pdPASS);
}

void furi_timer_set_thread_priority(FuriTimerThreadPriority priority) {
    furi_check(!furi_kernel_is_irq_or_masked());

    TaskHandle_t task_handle = xTimerGetTimerDaemonTaskHandle();
    furi_check(task_handle); // Don't call this method before timer task start

    if(priority == FuriTimerThreadPriorityNormal) {
        vTaskPrioritySet(task_handle, configTIMER_TASK_PRIORITY);
    } else if(priority == FuriTimerThreadPriorityElevated) {
        vTaskPrioritySet(task_handle, configMAX_PRIORITIES - 1);
    } else {
        furi_crash();
    }
}
