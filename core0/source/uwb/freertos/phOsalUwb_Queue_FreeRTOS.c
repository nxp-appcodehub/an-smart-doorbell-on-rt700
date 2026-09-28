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
 * phOsalUwb_Queue_FreeRTOS.c
 *
 * FreeRTOS port of the UWB IoT OSAL message-queue layer.
 * Replaces phOsalUwb_Queue_Zephyr.c.
 *
 * Zephyr -> FreeRTOS mapping
 *   k_msgq_init()          -> xQueueCreate()
 *   k_msgq_put(K_NO_WAIT)  -> xQueueSendFromISR() / xQueueSend(0)
 *   k_msgq_put(K_MSEC(t))  -> xQueueSend(pdMS_TO_TICKS(t))
 *   k_msgq_get(K_MSEC(t))  -> xQueueReceive(pdMS_TO_TICKS(t))
 *   k_msgq_num_free_get()  -> uxQueueSpacesAvailable()
 *   k_msgq_purge()         -> xQueueReset()
 */

#include "phOsalUwb.h"
#include "phOsalUwb_Internal.h"
#include "uwb_board.h"

#include <stdio.h>

/* FreeRTOS */
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

/* CMSIS-Core (ISR detection) */
#include "cmsis_compiler.h"

/* -------------------------------------------------------------------------
 * Internal helper: detect ISR context.
 * ------------------------------------------------------------------------- */
static inline int uwb_queue_is_in_isr(void)
{
#if defined(__CORTEX_M)
    return (int)((SCB->ICSR & SCB_ICSR_VECTACTIVE_Msk) != 0U);
#else
    return 0;
#endif
}

/* =========================================================================
 * Create message queue
 *
 * Returns an opaque handle (cast to intptr_t) on success, or 0 on failure.
 * The queue stores phLibUwb_Message_t items.
 * ========================================================================= */

intptr_t phOsalUwb_msgget(uint32_t queueLength)
{
    QueueHandle_t xQueue;

    if (queueLength == 0U)
    {
        return (intptr_t)0;
    }

    xQueue = xQueueCreate((UBaseType_t)queueLength,
                          (UBaseType_t)sizeof(phLibUwb_Message_t));
    return (intptr_t)xQueue;
}

/* =========================================================================
 * Release message queue
 * ========================================================================= */

void phOsalUwb_msgrelease(intptr_t msqid)
{
    QueueHandle_t xQueue = (QueueHandle_t)msqid;

    if (xQueue != NULL)
    {
        vQueueDelete(xQueue);
    }
}

/* =========================================================================
 * Send a message to the queue (ISR-safe).
 *
 * waittimeout: milliseconds to wait if queue is full (0 = no-wait).
 * ========================================================================= */

UWBSTATUS phOsalUwb_msgsnd(intptr_t msqid, phLibUwb_Message_t *msg, unsigned long waittimeout)
{
    QueueHandle_t xQueue = (QueueHandle_t)msqid;
    BaseType_t xRet;

    if (xQueue == NULL || msg == NULL)
    {
        return UWBSTATUS_FAILED;
    }

    if (uwb_queue_is_in_isr())
    {
        /* Non-blocking send from ISR */
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xRet = xQueueSendFromISR(xQueue, msg, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
    else
    {
        TickType_t ticks = (waittimeout == 0UL) ? (TickType_t)0U
                                                : pdMS_TO_TICKS(waittimeout);
        xRet = xQueueSend(xQueue, msg, ticks);
    }

    return (xRet == pdTRUE) ? UWBSTATUS_SUCCESS : UWBSTATUS_FAILED;
}

/* =========================================================================
 * Receive a message from the queue.
 *
 * waittimeout: milliseconds to wait (0 = no-wait, ~0UL = wait forever).
 * ========================================================================= */

UWBSTATUS phOsalUwb_msgrcv(intptr_t msqid, phLibUwb_Message_t *msg, unsigned long waittimeout)
{
    QueueHandle_t xQueue = (QueueHandle_t)msqid;
    BaseType_t xRet;

    if (xQueue == NULL || msg == NULL)
    {
        return UWBSTATUS_FAILED;
    }

    TickType_t ticks;
    if (waittimeout == (unsigned long)portMAX_DELAY)
    {
        ticks = portMAX_DELAY;
    }
    else
    {
        ticks = (waittimeout == 0UL) ? (TickType_t)0U : pdMS_TO_TICKS(waittimeout);
    }

    xRet = xQueueReceive(xQueue, msg, ticks);
    return (xRet == pdTRUE) ? UWBSTATUS_SUCCESS : UWBSTATUS_FAILED;
}

/* =========================================================================
 * Query free space in the queue.
 * ========================================================================= */

int phOsalUwb_queueSpacesAvailable(intptr_t msqid)
{
    QueueHandle_t xQueue = (QueueHandle_t)msqid;

    if (xQueue == NULL)
    {
        return 0;
    }

    return (int)uxQueueSpacesAvailable(xQueue);
}

/* =========================================================================
 * Reset (purge) the queue - discard all pending messages.
 * ========================================================================= */

UWBSTATUS phOsalUwb_msgreset(intptr_t msqid)
{
    QueueHandle_t xQueue = (QueueHandle_t)msqid;

    if (xQueue == NULL)
    {
        return UWBSTATUS_FAILED;
    }

    (void)xQueueReset(xQueue);
    return UWBSTATUS_SUCCESS;
}
