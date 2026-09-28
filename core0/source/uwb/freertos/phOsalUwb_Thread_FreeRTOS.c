/*
 * Copyright 2024-2025 NXP.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * phOsalUwb_Thread_FreeRTOS.c
 *
 * FreeRTOS port of the UWB IoT OSAL thread layer.
 * Replaces phOsalUwb_Thread_Zephyr.c.
 *
 * Zephyr -> FreeRTOS mapping
 *   k_thread_create()   -> xTaskCreate()
 *   k_thread_abort()    -> vTaskDelete()
 *   k_thread_join()     -> (no direct equiv - use notification or deletion flag)
 *   k_thread_suspend()  -> vTaskSuspend()
 *   k_thread_resume()   -> vTaskResume()
 *   k_yield()           -> taskYIELD()
 *   k_can_yield()       -> always TRUE in task context
 *   k_is_in_isr()       -> CMSIS SCB->ICSR
 *
 * Stack notes:
 *   Zephyr stacks are byte-addressed; FreeRTOS stack depth is in stack words
 *   (sizeof(StackType_t), typically 4 bytes on ARM).
 *   phOsalUwb_ThreadCreationParams_t.stackdepth is in bytes, so it is divided
 *   by sizeof(StackType_t) when passed to xTaskCreate().
 *
 *   The Zephyr port required a statically declared K_THREAD_STACK_DEFINE
 *   object to be provided in pStack.  On FreeRTOS the stack is allocated
 *   dynamically by the kernel, so pStack is unused and may be left NULL.
 */

#ifdef UWBIOT_USE_FTR_FILE
#include "uwb_iot_ftr.h"
#else
#include "uwb_iot_ftr_default.h"
#endif

#include "phOsalUwb.h"
#include "phOsalUwb_Internal.h"
#include "phNxpLogApis_HalUtils.h"

/* FreeRTOS */
#include "FreeRTOS.h"
#include "task.h"

/* CMSIS-Core (ISR detection on ARM Cortex-M) */
#include "cmsis_compiler.h"

/* Default task parameters when caller does not supply them */
#define UWB_DEFAULT_TASK_STACK_WORDS  512U   /* words = 2 kB on ARM */
#define UWB_DEFAULT_TASK_PRIORITY     3U     /* lower number = lower priority in FreeRTOS */

/* -------------------------------------------------------------------------
 * Internal helper: detect ISR context via CMSIS SCB->ICSR.
 * ------------------------------------------------------------------------- */
static inline int uwb_thread_is_in_isr(void)
{
#if defined(__CORTEX_M)
    return (int)((SCB->ICSR & SCB_ICSR_VECTACTIVE_Msk) != 0U);
#else
    return 0;
#endif
}

/* =========================================================================
 * Thread create
 * ========================================================================= */

UWBSTATUS phOsalUwb_Thread_Create(void **hThread,
                                   pphOsalUwb_ThreadFunction_t pThreadFunction,
                                   void *pParam)
{
    UWBSTATUS wCreateStatus = UWBSTATUS_FAILED;
    phOsalUwb_ThreadCreationParams_t *pThreadParams =
        (phOsalUwb_ThreadCreationParams_t *)pParam;
    phOsalUwb_sOsalThreadHandle_t *pOsalThreadHandle = NULL;
    UBaseType_t uxPriority;
    uint32_t usStackDepthWords;
    BaseType_t xRet;

    if (uwb_thread_is_in_isr())
    {
        return PHUWBSTVAL(CID_UWB_OSAL, UWBSTATUS_PENDING);
    }

    if ((NULL == hThread) || (NULL == pThreadFunction) || (NULL == pThreadParams))
    {
        return PHUWBSTVAL(CID_UWB_OSAL, UWBSTATUS_INVALID_PARAMETER);
    }

    *hThread = NULL;

    /* Allocate handle wrapper */
    pOsalThreadHandle = (phOsalUwb_sOsalThreadHandle_t *)
        phOsalUwb_GetMemory(sizeof(phOsalUwb_sOsalThreadHandle_t));
    if (pOsalThreadHandle == NULL)
    {
        return PHUWBSTVAL(CID_UWB_OSAL, PH_OSALUWB_THREAD_CREATION_ERROR);
    }

    pOsalThreadHandle->ThreadFunction = pThreadFunction;
    pOsalThreadHandle->Params         = pThreadParams->pContext;
    pOsalThreadHandle->ObjectHandle   = NULL;

    /* Convert stack bytes to FreeRTOS stack words */
    usStackDepthWords = (pThreadParams->stackdepth > 0U)
        ? (uint32_t)(pThreadParams->stackdepth / sizeof(StackType_t))
        : UWB_DEFAULT_TASK_STACK_WORDS;

    /* Map UWB middleware priority to FreeRTOS priority.
     * The Zephyr port used K_PRIO_PREEMPT(priority) where lower number =
     * lower urgency.  FreeRTOS also uses lower number for lower priority, so
     * a direct mapping is correct.  Clamp to configMAX_PRIORITIES - 1. */
    uxPriority = (pThreadParams->priority < (uint16_t)(configMAX_PRIORITIES - 1))
        ? (UBaseType_t)pThreadParams->priority
        : (UBaseType_t)(configMAX_PRIORITIES - 1);

    if (uxPriority == 0U)
    {
        uxPriority = (UBaseType_t)UWB_DEFAULT_TASK_PRIORITY;
    }

    /* FreeRTOS task entry must be void (*)(void*) - matching pphOsalUwb_ThreadFunction_t */
    xRet = xTaskCreate(pThreadFunction,
                        pThreadParams->taskname,
                        (configSTACK_DEPTH_TYPE)usStackDepthWords,
                        pThreadParams->pContext,
                        uxPriority,
                        &pOsalThreadHandle->ObjectHandle);

    if (xRet != pdPASS || pOsalThreadHandle->ObjectHandle == NULL)
    {
        phOsalUwb_FreeMemory(pOsalThreadHandle);
        return PHUWBSTVAL(CID_UWB_OSAL, PH_OSALUWB_THREAD_CREATION_ERROR);
    }

    *hThread      = (void *)pOsalThreadHandle;
    wCreateStatus = UWBSTATUS_SUCCESS;
    return wCreateStatus;
}

/* =========================================================================
 * Thread delete
 * ========================================================================= */

UWBSTATUS phOsalUwb_Thread_Delete(UWBOSAL_TASK_HANDLE hThreadRaw)
{
    /* The middleware stores a phOsalUwb_sOsalThreadHandle_t* in the opaque handle */
    phOsalUwb_sOsalThreadHandle_t *hThread =
        (phOsalUwb_sOsalThreadHandle_t *)hThreadRaw;
    UWBSTATUS wDeletionStatus = UWBSTATUS_FAILED;

    if (hThread == NULL)
    {
        return PHUWBSTVAL(CID_UWB_OSAL, PH_OSALUWB_THREAD_DELETE_ERROR);
    }

    if (hThread->ObjectHandle != NULL)
    {
        /* vTaskDelete(NULL) would delete the calling task; pass the stored handle. */
        vTaskDelete(hThread->ObjectHandle);
        hThread->ObjectHandle = NULL;
    }

    phOsalUwb_FreeMemory(hThread);
    wDeletionStatus = UWBSTATUS_SUCCESS;
    return wDeletionStatus;
}

/* =========================================================================
 * Thread join  (FreeRTOS has no native join; the calling task blocks via
 *               a task notification sent by the target task at exit, or we
 *               poll.  The middleware calls Join only to wait for clean exit,
 *               so a short busy-wait-with-yield is acceptable here.)
 * ========================================================================= */

void phOsalUwb_Thread_Join(UWBOSAL_TASK_HANDLE hThreadRaw)
{
    phOsalUwb_sOsalThreadHandle_t *hThread =
        (phOsalUwb_sOsalThreadHandle_t *)hThreadRaw;
    /* FreeRTOS does not have a thread join primitive.
     * Spin-yield until the target task handle is no longer valid. */
    if (hThread == NULL)
    {
        return;
    }

    /* If the task was already deleted the handle is NULL. */
    while (hThread->ObjectHandle != NULL)
    {
        /* Give up the CPU so the target task can run to completion */
        taskYIELD();
    }
}

/* =========================================================================
 * Suspend / Resume / Yield
 * ========================================================================= */

void phOsalUwb_TaskSuspend(UWBOSAL_TASK_HANDLE hThreadRaw)
{
    phOsalUwb_sOsalThreadHandle_t *hThread =
        (phOsalUwb_sOsalThreadHandle_t *)hThreadRaw;
    if (hThread != NULL && hThread->ObjectHandle != NULL)
    {
        vTaskSuspend(hThread->ObjectHandle);
    }
}

void phOsalUwb_TaskResume(UWBOSAL_TASK_HANDLE hThreadRaw)
{
    phOsalUwb_sOsalThreadHandle_t *hThread =
        (phOsalUwb_sOsalThreadHandle_t *)hThreadRaw;
    if (hThread != NULL && hThread->ObjectHandle != NULL)
    {
        if (uwb_thread_is_in_isr())
        {
            (void)xTaskResumeFromISR(hThread->ObjectHandle);
            /* Yield handled by caller's ISR epilogue */
        }
        else
        {
            vTaskResume(hThread->ObjectHandle);
        }
    }
}

void phOsalUwb_TaskStartScheduler(void)
{
    /* In FreeRTOS the scheduler is started once from main() via
     * vTaskStartScheduler().  This OSAL function is called as a
     * cooperative-yield hint; map to taskYIELD(). */
    taskYIELD();
}

void phOsalUwb_Thread_Context_Switch(void)
{
    /* Request a context switch when not inside an ISR */
    if (!uwb_thread_is_in_isr())
    {
        taskYIELD();
    }
}

/* =========================================================================
 * Get current task handle
 * ========================================================================= */

UWBOSAL_TASK_HANDLE phOsalUwb_GetTaskHandle(void)
{
    /* Return a minimal wrapper holding the current task handle.
     * The caller owns this memory and should free it after use.
     * Cast to UWBOSAL_TASK_HANDLE (TaskHandle_t) - middleware treats it as opaque. */
    phOsalUwb_sOsalThreadHandle_t *pHandle =
        (phOsalUwb_sOsalThreadHandle_t *)phOsalUwb_GetMemory(sizeof(phOsalUwb_sOsalThreadHandle_t));

    if (pHandle != NULL)
    {
        pHandle->ObjectHandle   = xTaskGetCurrentTaskHandle();
        pHandle->ThreadFunction = NULL;
        pHandle->Params         = NULL;
    }

    return (UWBOSAL_TASK_HANDLE)pHandle;
}
