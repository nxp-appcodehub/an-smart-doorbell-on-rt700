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
 * phOsalUwb_FreeRTOS.c
 *
 * FreeRTOS port of the UWB IoT OSAL layer.
 * Replaces phOsalUwb_Zephyr.c for MCUXpresso SDK + FreeRTOS targets.
 *
 * Maps:
 *   k_sem_*      -> xSemaphoreCreate*, xSemaphoreGive/Take, vSemaphoreDelete
 *   k_mutex_*    -> xSemaphoreCreateMutex, xSemaphoreTake/Give (portMAX_DELAY)
 *   k_msleep     -> vTaskDelay(pdMS_TO_TICKS())
 *   sys_clock_tick_get -> xTaskGetTickCount()
 *   k_is_in_isr  -> CMSIS SCB->ICSR check
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* FreeRTOS kernel headers */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* CMSIS-Core (for ISR detection on ARM Cortex-M) */
#include "cmsis_compiler.h"

/* UWB OSAL headers */
#include "phNxpUciHal_utils.h"
#include "phOsalUwb.h"
#include "phOsalUwb_Internal.h"
#include "phUwb_BuildConfig.h"
#include "uwb_board.h"

/* -------------------------------------------------------------------------
 * Internal helper: detect if execution is currently inside an ISR.
 * Uses CMSIS SCB->ICSR VECTACTIVE field (bits[8:0]).
 * Returns non-zero when inside an interrupt service routine.
 * ------------------------------------------------------------------------- */
static inline int uwb_freertos_is_in_isr(void)
{
#if defined(__CORTEX_M)
    return (int)((SCB->ICSR & SCB_ICSR_VECTACTIVE_Msk) != 0U);
#else
    /* Non-Cortex-M fallback: always task context */
    return 0;
#endif
}

/* =========================================================================
 * Memory helpers - use standard heap (backed by FreeRTOS heap_x.c or
 * system malloc depending on the SDK configuration).
 * ========================================================================= */

void *phOsalUwb_GetMemory(uint32_t dwSize)
{
    return pvPortMalloc((size_t)dwSize);
}

void phOsalUwb_FreeMemory(void *pMem)
{
    if (pMem != NULL)
    {
        vPortFree(pMem);
    }
}

int32_t phOsalUwb_MemCompare(const void *pDest, const void *pSrc, uint32_t dwSize)
{
    return (int32_t)memcmp(pDest, pSrc, (size_t)dwSize);
}

void phOsalUwb_SetMemory(void *pMem, uint8_t bVal, uint32_t dwSize)
{
    (void)memset(pMem, (int)bVal, (size_t)dwSize);
}

void phOsalUwb_MemCopy(void *pDest, const void *pSrc, uint32_t dwSize)
{
    (void)memcpy(pDest, pSrc, (size_t)dwSize);
}

/* =========================================================================
 * Counting semaphore  (Zephyr: k_sem_init with max=1 used as binary,
 *                      or with bInitialValue > 0 as counting)
 *
 * The UWB middleware always creates semaphores with maxCount=1, so
 * xSemaphoreCreateBinary gives identical behaviour.  The bInitialValue
 * parameter controls whether the semaphore starts taken (0) or given (1).
 * ========================================================================= */

UWBSTATUS phOsalUwb_CreateSemaphore(void **hSemaphore, uint8_t bInitialValue)
{
    UWBSTATUS wCreateStatus                      = UWBSTATUS_FAILED;
    phOsalUwb_sOsalSemaphore_t *pSemaphoreHandle = NULL;

    if (uwb_freertos_is_in_isr())
    {
        /* Semaphore creation is not allowed inside an ISR */
        wCreateStatus = PHUWBSTVAL(CID_UWB_OSAL, PH_OSALUWB_SEMAPHORE_CREATION_ERROR);
        return wCreateStatus;
    }

    if (NULL == hSemaphore)
    {
        return PHUWBSTVAL(CID_UWB_OSAL, UWBSTATUS_INVALID_PARAMETER);
    }

    pSemaphoreHandle =
        (phOsalUwb_sOsalSemaphore_t *)phOsalUwb_GetMemory(sizeof(phOsalUwb_sOsalSemaphore_t));
    if (pSemaphoreHandle == NULL)
    {
        return PHUWBSTVAL(CID_UWB_OSAL, PH_OSALUWB_SEMAPHORE_CREATION_ERROR);
    }

    /* Create a binary semaphore (max count = 1). */
    pSemaphoreHandle->ObjectHandle = xSemaphoreCreateBinary();
    if (pSemaphoreHandle->ObjectHandle == NULL)
    {
        phOsalUwb_FreeMemory(pSemaphoreHandle);
        return PHUWBSTVAL(CID_UWB_OSAL, PH_OSALUWB_SEMAPHORE_CREATION_ERROR);
    }

    /* Give once if the caller wants initial count = 1. */
    if (bInitialValue != 0U)
    {
        (void)xSemaphoreGive(pSemaphoreHandle->ObjectHandle);
    }

    *hSemaphore  = (void *)pSemaphoreHandle;
    wCreateStatus = UWBSTATUS_SUCCESS;
    return wCreateStatus;
}

UWBSTATUS phOsalUwb_ProduceSemaphore(void *hSemaphore)
{
    UWBSTATUS wReleaseStatus = UWBSTATUS_FAILED;
    phOsalUwb_sOsalSemaphore_t *pSemaphoreHandle = (phOsalUwb_sOsalSemaphore_t *)hSemaphore;
    BaseType_t xHigherPriorityTaskWoken          = pdFALSE;

    if (pSemaphoreHandle == NULL || pSemaphoreHandle->ObjectHandle == NULL)
    {
        return PHUWBSTVAL(CID_UWB_OSAL, UWBSTATUS_INVALID_PARAMETER);
    }

    if (uwb_freertos_is_in_isr())
    {
        /* Give from ISR context - used by SPI/GPIO callbacks */
        (void)xSemaphoreGiveFromISR(pSemaphoreHandle->ObjectHandle, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
    else
    {
        (void)xSemaphoreGive(pSemaphoreHandle->ObjectHandle);
    }

    wReleaseStatus = UWBSTATUS_SUCCESS;
    return wReleaseStatus;
}

UWBSTATUS phOsalUwb_ConsumeSemaphore_WithTimeout(void *hSemaphore, uint32_t delayms)
{
    UWBSTATUS wConsumeStatus = UWBSTATUS_FAILED;
    phOsalUwb_sOsalSemaphore_t *pSemaphoreHandle = (phOsalUwb_sOsalSemaphore_t *)hSemaphore;
    BaseType_t ret;

    if (pSemaphoreHandle == NULL || pSemaphoreHandle->ObjectHandle == NULL)
    {
        return PHUWBSTVAL(CID_UWB_OSAL, UWBSTATUS_INVALID_PARAMETER);
    }

    if (uwb_freertos_is_in_isr())
    {
        /* Non-blocking take inside ISR */
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        ret = xSemaphoreTakeFromISR(pSemaphoreHandle->ObjectHandle, &xHigherPriorityTaskWoken);
    }
    else
    {
        TickType_t ticks = (delayms == 0U) ? 0U : pdMS_TO_TICKS(delayms);
        ret = xSemaphoreTake(pSemaphoreHandle->ObjectHandle, ticks);
    }

    if (ret != pdTRUE)
    {
        wConsumeStatus = PHUWBSTVAL(CID_UWB_OSAL, PH_OSALUWB_SEMAPHORE_CONSUME_ERROR);
    }
    else
    {
        wConsumeStatus = UWBSTATUS_SUCCESS;
    }
    return wConsumeStatus;
}

UWBSTATUS phOsalUwb_DeleteSemaphore(void **hSemaphore)
{
    UWBSTATUS wDeletionStatus = UWBSTATUS_FAILED;
    phOsalUwb_sOsalSemaphore_t *pSemaphoreHandle;

    if (uwb_freertos_is_in_isr())
    {
        return PHUWBSTVAL(CID_UWB_OSAL, PH_OSALUWB_MUTEX_DELETE_ERROR);
    }

    if (hSemaphore == NULL || *hSemaphore == NULL)
    {
        return PHUWBSTVAL(CID_UWB_OSAL, UWBSTATUS_INVALID_PARAMETER);
    }

    pSemaphoreHandle = (phOsalUwb_sOsalSemaphore_t *)*hSemaphore;

    if (pSemaphoreHandle->ObjectHandle != NULL)
    {
        vSemaphoreDelete(pSemaphoreHandle->ObjectHandle);
        pSemaphoreHandle->ObjectHandle = NULL;
    }
    phOsalUwb_FreeMemory(pSemaphoreHandle);
    *hSemaphore       = NULL;
    wDeletionStatus   = UWBSTATUS_SUCCESS;
    return wDeletionStatus;
}

/* =========================================================================
 * Binary semaphore  (Zephyr: k_sem_init(sem, 0, 1) - starts in taken state)
 * ========================================================================= */

UWBSTATUS phOsalUwb_CreateBinSem(void **hBinSem)
{
    /* A binary semaphore with initial count 0 (caller must Give before Take). */
    return phOsalUwb_CreateSemaphore(hBinSem, 0U);
}

/* =========================================================================
 * Mutex  (recursive mutex via FreeRTOS xSemaphoreCreateMutex)
 * ========================================================================= */

UWBSTATUS phOsalUwb_CreateMutex(void **hMutex)
{
    UWBSTATUS wCreateStatus          = UWBSTATUS_FAILED;
    phOsalUwb_sOsalMutex_t *pMutexHandle = NULL;

    if (uwb_freertos_is_in_isr())
    {
        /* Mutex creation is not allowed inside an ISR */
        return PHUWBSTVAL(CID_UWB_OSAL, PH_OSALUWB_MUTEX_CREATION_ERROR);
    }

    if (NULL == hMutex)
    {
        return PHUWBSTVAL(CID_UWB_OSAL, UWBSTATUS_INVALID_PARAMETER);
    }

    pMutexHandle = (phOsalUwb_sOsalMutex_t *)phOsalUwb_GetMemory(sizeof(phOsalUwb_sOsalMutex_t));
    if (pMutexHandle == NULL)
    {
        return PHUWBSTVAL(CID_UWB_OSAL, PH_OSALUWB_MUTEX_CREATION_ERROR);
    }

    /* Use a standard (non-recursive) mutex. The UWB middleware does not
     * require recursive locking. */
    pMutexHandle->ObjectHandle = xSemaphoreCreateMutex();
    if (pMutexHandle->ObjectHandle == NULL)
    {
        phOsalUwb_FreeMemory(pMutexHandle);
        return PHUWBSTVAL(CID_UWB_OSAL, PH_OSALUWB_MUTEX_CREATION_ERROR);
    }

    *hMutex       = (void *)pMutexHandle;
    wCreateStatus = UWBSTATUS_SUCCESS;
    return wCreateStatus;
}

UWBSTATUS phOsalUwb_LockMutex(void *hMutex)
{
    UWBSTATUS wLockStatus            = UWBSTATUS_FAILED;
    phOsalUwb_sOsalMutex_t *pMutexHandle = (phOsalUwb_sOsalMutex_t *)hMutex;

    if (pMutexHandle == NULL || pMutexHandle->ObjectHandle == NULL)
    {
        return PHUWBSTVAL(CID_UWB_OSAL, UWBSTATUS_INVALID_PARAMETER);
    }

    if (uwb_freertos_is_in_isr())
    {
        /* Mutex locking is not allowed inside an ISR */
        return PHUWBSTVAL(CID_UWB_OSAL, PH_OSALUWB_MUTEX_LOCK_ERROR);
    }

    if (xSemaphoreTake(pMutexHandle->ObjectHandle, portMAX_DELAY) != pdTRUE)
    {
        wLockStatus = PHUWBSTVAL(CID_UWB_OSAL, PH_OSALUWB_MUTEX_LOCK_ERROR);
    }
    else
    {
        wLockStatus = UWBSTATUS_SUCCESS;
    }
    return wLockStatus;
}

UWBSTATUS phOsalUwb_UnlockMutex(void *hMutex)
{
    UWBSTATUS wUnlockStatus          = UWBSTATUS_FAILED;
    phOsalUwb_sOsalMutex_t *pMutexHandle = (phOsalUwb_sOsalMutex_t *)hMutex;

    if (pMutexHandle == NULL || pMutexHandle->ObjectHandle == NULL)
    {
        return PHUWBSTVAL(CID_UWB_OSAL, UWBSTATUS_INVALID_PARAMETER);
    }

    if (uwb_freertos_is_in_isr())
    {
        /* Mutex unlocking is not allowed inside an ISR */
        return PHUWBSTVAL(CID_UWB_OSAL, PH_OSALUWB_MUTEX_UNLOCK_ERROR);
    }

    if (xSemaphoreGive(pMutexHandle->ObjectHandle) != pdTRUE)
    {
        wUnlockStatus = PHUWBSTVAL(CID_UWB_OSAL, PH_OSALUWB_MUTEX_UNLOCK_ERROR);
    }
    else
    {
        wUnlockStatus = UWBSTATUS_SUCCESS;
    }
    return wUnlockStatus;
}

UWBSTATUS phOsalUwb_DeleteMutex(void **hMutex)
{
    UWBSTATUS wDeletionStatus        = UWBSTATUS_FAILED;
    phOsalUwb_sOsalMutex_t *pMutexHandle;

    if (hMutex == NULL || *hMutex == NULL)
    {
        return PHUWBSTVAL(CID_UWB_OSAL, UWBSTATUS_INVALID_PARAMETER);
    }

    pMutexHandle = (phOsalUwb_sOsalMutex_t *)*hMutex;
    if (pMutexHandle->ObjectHandle != NULL)
    {
        vSemaphoreDelete(pMutexHandle->ObjectHandle);
        pMutexHandle->ObjectHandle = NULL;
    }
    phOsalUwb_FreeMemory(pMutexHandle);
    *hMutex         = NULL;
    wDeletionStatus = UWBSTATUS_SUCCESS;
    return wDeletionStatus;
}

/* =========================================================================
 * Delay and tick helpers
 * ========================================================================= */

void phOsalUwb_Delay(uint32_t dwDelay)
{
    vTaskDelay(pdMS_TO_TICKS(dwDelay));
}

void phOsalUwb_GetTickCount(unsigned long *pTickCount)
{
    if (pTickCount != NULL)
    {
        *pTickCount = (unsigned long)xTaskGetTickCount();
    }
}
