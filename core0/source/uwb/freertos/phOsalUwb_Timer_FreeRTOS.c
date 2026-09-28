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
 * phOsalUwb_Timer_FreeRTOS.c
 *
 * FreeRTOS port of the UWB IoT OSAL software-timer layer.
 * Replaces phOsalUwb_Timer_Zephyr.c.
 *
 * Zephyr -> FreeRTOS mapping
 *   k_timer_init(t, cb, NULL) -> xTimerCreate(..., cb)
 *   k_timer_start(t, dur, 0)  -> xTimerChangePeriod + xTimerStart (one-shot)
 *   k_timer_start(t, dur, p)  -> xTimerCreate with uxAutoReload=pdTRUE
 *   k_timer_stop(t)           -> xTimerStop()
 *   k_is_in_isr()             -> CMSIS SCB->ICSR check
 *
 * Implementation notes:
 *   - The UWB middleware uses at most PH_UWB_MAX_TIMER (= 5) simultaneous
 *     timers, all with millisecond resolution.
 *   - Callbacks are invoked from the FreeRTOS timer task (timer daemon),
 *     which is a task context, not an ISR.  This matches the Zephyr behaviour
 *     where the expiry function is also called from the system work queue.
 *   - Timer IDs are indices into the static apTimerInfo[] array, matching
 *     the pattern used in the Zephyr implementation.
 */

#ifndef COMPANION_DEVICE

#include "phOsalUwb.h"
#include "phUwbCommon.h"
#include "phUwbTypes.h"
#include "phNxpLogApis_TmlUwb.h"

/* FreeRTOS */
#include "FreeRTOS.h"
#include "timers.h"
#include "task.h"

/* CMSIS-Core (ISR detection on ARM Cortex-M) */
#include "cmsis_compiler.h"

/* -------------------------------------------------------------------------
 * Static timer info table - same pattern as the Zephyr port.
 * ------------------------------------------------------------------------- */
static phOsalUwb_TimerHandle_t apTimerInfo[PH_UWB_MAX_TIMER];

/* Forward declarations */
static void phOsalUwb_Timer_Expired(TimerHandle_t xTimer);

/* -------------------------------------------------------------------------
 * ISR detection helper
 * ------------------------------------------------------------------------- */
static inline int uwb_timer_is_in_isr(void)
{
#if defined(__CORTEX_M)
    return (int)((SCB->ICSR & SCB_ICSR_VECTACTIVE_Msk) != 0U);
#else
    return 0;
#endif
}

/* =========================================================================
 * Timer create
 * ========================================================================= */

uint32_t phOsalUwb_Timer_Create(bool bAutoReload)
{
    phOsalUwb_TimerHandle_t *pTimerHandle;
    TimerHandle_t xTimer;

    if (uwb_timer_is_in_isr())
    {
        return PH_OSALUWB_TIMER_CREATE_ERROR;
    }

    uint32_t dwTimerId = phUtilUwb_CheckForAvailableTimer();

    if (dwTimerId < PH_UWB_MAX_TIMER)
    {
        pTimerHandle = &apTimerInfo[dwTimerId];

        /* Create a FreeRTOS software timer.
         * Period is set to 1 tick as a placeholder; it will be reconfigured in
         * phOsalUwb_Timer_Start.  The timer is created dormant (not started). */
        xTimer = xTimerCreate(
            "uwbtmr",                            /* human-readable name */
            (TickType_t)1,                       /* initial period (overridden at start) */
            bAutoReload ? pdTRUE : pdFALSE,      /* auto-reload */
            (void *)(uintptr_t)dwTimerId,        /* timer ID = index into apTimerInfo */
            phOsalUwb_Timer_Expired);            /* expiry callback */

        if (xTimer == NULL)
        {
            return PH_OSALUWB_TIMER_START_ERROR;
        }

        pTimerHandle->hTimerHandle = (void *)xTimer;
        pTimerHandle->eState       = eTimerIdle;
        pTimerHandle->TimerId      = dwTimerId;
    }
    else
    {
        dwTimerId = PH_OSALUWB_TIMER_ID_INVALID;
    }

    return dwTimerId;
}

/* =========================================================================
 * Timer start
 * ========================================================================= */

UWBSTATUS phOsalUwb_Timer_Start(uint32_t dwTimerId,
                                  uint32_t dwRegTimeCnt,
                                  pphOsalUwb_TimerCallbck_t pApplication_callback,
                                  void *pContext)
{
    UWBSTATUS wStartStatus = UWBSTATUS_SUCCESS;
    phOsalUwb_TimerHandle_t *pTimerHandle;

    if (uwb_timer_is_in_isr())
    {
        return PH_OSALUWB_TIMER_START_ERROR;
    }

    if (dwTimerId >= PH_UWB_MAX_TIMER)
    {
        return PHUWBSTVAL(CID_UWB_OSAL, UWBSTATUS_INVALID_PARAMETER);
    }

    pTimerHandle = &apTimerInfo[dwTimerId];

    if ((pApplication_callback == NULL) || (pTimerHandle->hTimerHandle == NULL))
    {
        return PHUWBSTVAL(CID_UWB_OSAL, UWBSTATUS_INVALID_PARAMETER);
    }

    pTimerHandle->Application_callback = pApplication_callback;
    pTimerHandle->pContext             = pContext;
    pTimerHandle->eState               = eTimerRunning;

    /* Update the period and start the timer.
     * xTimerChangePeriod also starts a dormant timer. */
    TickType_t ticks = pdMS_TO_TICKS(dwRegTimeCnt);
    if (ticks == 0U)
    {
        ticks = 1U; /* FreeRTOS requires period >= 1 tick */
    }

    if (xTimerChangePeriod((TimerHandle_t)pTimerHandle->hTimerHandle,
                           ticks,
                           pdMS_TO_TICKS(100)) != pdPASS)
    {
        pTimerHandle->eState = eTimerIdle;
        wStartStatus = PHUWBSTVAL(CID_UWB_OSAL, UWBSTATUS_INVALID_PARAMETER);
    }

    return wStartStatus;
}

/* =========================================================================
 * Timer stop
 * ========================================================================= */

UWBSTATUS phOsalUwb_Timer_Stop(uint32_t dwTimerId)
{
    UWBSTATUS wStopStatus = UWBSTATUS_SUCCESS;
    phOsalUwb_TimerHandle_t *pTimerHandle;

    if (dwTimerId >= PH_UWB_MAX_TIMER)
    {
        return PHUWBSTVAL(CID_UWB_OSAL, UWBSTATUS_INVALID_PARAMETER);
    }

    pTimerHandle = &apTimerInfo[dwTimerId];

    if (pTimerHandle->hTimerHandle == NULL)
    {
        return PHUWBSTVAL(CID_UWB_OSAL, UWBSTATUS_INVALID_PARAMETER);
    }

    if (pTimerHandle->eState == eTimerStopped)
    {
        /* Already stopped - no action required */
        return UWBSTATUS_SUCCESS;
    }

    if (pTimerHandle->eState == eTimerRunning)
    {
        if (xTimerStop((TimerHandle_t)pTimerHandle->hTimerHandle,
                       pdMS_TO_TICKS(100)) != pdPASS)
        {
            wStopStatus = PHUWBSTVAL(CID_UWB_OSAL, UWBSTATUS_INVALID_PARAMETER);
        }
        else
        {
            pTimerHandle->eState = eTimerStopped;
        }
    }
    else
    {
        wStopStatus = PHUWBSTVAL(CID_UWB_OSAL, UWBSTATUS_INVALID_PARAMETER);
    }

    return wStopStatus;
}

/* =========================================================================
 * Timer delete
 * ========================================================================= */

UWBSTATUS phOsalUwb_Timer_Delete(uint32_t dwTimerId)
{
    UWBSTATUS wDeleteStatus = UWBSTATUS_SUCCESS;
    phOsalUwb_TimerHandle_t *pTimerHandle;

    if (dwTimerId >= PH_UWB_MAX_TIMER)
    {
        return PHUWBSTVAL(CID_UWB_OSAL, UWBSTATUS_INVALID_PARAMETER);
    }

    pTimerHandle = &apTimerInfo[dwTimerId];

    if (pTimerHandle->hTimerHandle == NULL)
    {
        /* Already deleted */
        return UWBSTATUS_SUCCESS;
    }

    if (UWBSTATUS_SUCCESS != phOsalUwb_CheckTimerPresence(pTimerHandle))
    {
        return PHUWBSTVAL(CID_UWB_OSAL, UWBSTATUS_INVALID_PARAMETER);
    }

    /* Stop the timer first if it is running */
    if (pTimerHandle->eState != eTimerStopped)
    {
        (void)xTimerStop((TimerHandle_t)pTimerHandle->hTimerHandle, pdMS_TO_TICKS(100));
    }

    /* Delete the FreeRTOS timer object */
    (void)xTimerDelete((TimerHandle_t)pTimerHandle->hTimerHandle, pdMS_TO_TICKS(100));

    /* Zero out the entry so it becomes available again */
    phOsalUwb_SetMemory(pTimerHandle, 0x00U, sizeof(phOsalUwb_TimerHandle_t));

    return wDeleteStatus;
}

/* =========================================================================
 * Timer expiry callback - called from the FreeRTOS timer daemon task.
 * ========================================================================= */

static void phOsalUwb_Timer_Expired(TimerHandle_t xTimer)
{
    /* Recover the timer index stored as the timer ID */
    uint32_t dwTimerId = (uint32_t)(uintptr_t)pvTimerGetTimerID(xTimer);
    phOsalUwb_TimerHandle_t *pTimerHandle = NULL;

    if (dwTimerId < PH_UWB_MAX_TIMER)
    {
        pTimerHandle = &apTimerInfo[dwTimerId];
    }

    if (pTimerHandle != NULL && pTimerHandle->hTimerHandle != NULL)
    {
        pTimerHandle->eState = eTimerStopped;
        if (pTimerHandle->Application_callback != NULL)
        {
            pTimerHandle->Application_callback(pTimerHandle->TimerId, pTimerHandle->pContext);
        }
    }
}

/* =========================================================================
 * Helper: scan apTimerInfo for a free slot (hTimerHandle == NULL).
 * Returns the slot index, or PH_OSALUWB_TIMER_ID_INVALID when all are used.
 * ========================================================================= */

uint32_t phUtilUwb_CheckForAvailableTimer(void)
{
    uint32_t dwRetval;

    for (dwRetval = 0U; dwRetval < PH_UWB_MAX_TIMER; dwRetval++)
    {
        if (apTimerInfo[dwRetval].hTimerHandle == NULL)
        {
            break;
        }
    }

    if (dwRetval >= PH_UWB_MAX_TIMER)
    {
        dwRetval = PH_OSALUWB_TIMER_ID_INVALID;
    }

    return dwRetval;
}

/* =========================================================================
 * Helper: verify a timer handle is present in the table.
 * ========================================================================= */

UWBSTATUS phOsalUwb_CheckTimerPresence(void *pObjectHandle)
{
    uint32_t dwIndex;
    UWBSTATUS wStatus = UWBSTATUS_INVALID_PARAMETER;

    for (dwIndex = 0U; dwIndex < PH_UWB_MAX_TIMER; dwIndex++)
    {
        if (&apTimerInfo[dwIndex] == (phOsalUwb_TimerHandle_t *)pObjectHandle)
        {
            wStatus = UWBSTATUS_SUCCESS;
            break;
        }
    }

    return wStatus;
}

#endif /* COMPANION_DEVICE */
