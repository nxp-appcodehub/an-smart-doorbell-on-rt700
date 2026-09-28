/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "FreeRTOS.h"
#include "queue.h"
#include "fsl_debug_console.h"
#include "app_msg.h"
#include "mpp_app_task_notify.h"

static QueueHandle_t s_MppTaskQueue = NULL;

void mpp_task_set_queue(QueueHandle_t queue)
{
    s_MppTaskQueue = queue;
}


void mpp_task_notify(uint8_t cmd, msg_t* para)
{
	PRINTF("sent msg:%d to mpp task\r\n", cmd);

	if (s_MppTaskQueue == NULL)
	{
		PRINTF("Error: Control task queue not initialized\r\n");
		if (para != NULL) vPortFree(para);
		return;
	}

	if (para == NULL) {
		para = (msg_t*)pvPortMalloc(sizeof(msg_t));
		assert(para != NULL);  // Ensure memory allocation was successful
	}

	para->msgID = cmd;

	if (__get_IPSR() == 0)
	{
		xQueueSend( s_MppTaskQueue, &para, portMAX_DELAY );
	}
	else
	{
		BaseType_t xHigherPriorityTaskWoken = pdFALSE;
		xQueueSendFromISR( s_MppTaskQueue, &para, &xHigherPriorityTaskWoken );
		if (xHigherPriorityTaskWoken)
		{
			portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
		}
	}
}
