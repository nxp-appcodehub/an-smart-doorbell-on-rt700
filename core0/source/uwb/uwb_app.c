/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*******************************************************************************
 * Includes
 ******************************************************************************/
/* FreeRTOS kernel includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"

/* Freescale includes. */
#include "fsl_device_registers.h"
#include "fsl_debug_console.h"
#include "board.h"
#include "clock_config.h"
#include "app_msg.h"

#include "phOsalUwb.h"

#include "uwb_app.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/**
 * @brief Queue used to receive sensitivity commands from the shell task.
 *
 * shell_task.c sends a float (sensitivity value / 10) via this queue when the
 * "set_uwb_sensitivity" shell command is executed.  The UWB demo task reads it
 * and adjusts the detection threshold accordingly.
 */
QueueHandle_t shellCmdQueue;

/**
 * @brief Task handle for the UWB demo task
 *
 * Global handle used to manage the UWB demonstration task lifecycle.
 * This handle is set when the UWB demo task is started and can be used
 * for task control operations.
 */
UWBOSAL_TASK_HANDLE testTaskHandle;
UWBOSAL_TASK_HANDLE uwb_demo_start(void);
/*******************************************************************************
 * Code
 ******************************************************************************/
void uwb_init()
{
    /* SPI clock attach, LP_FLEXCOMM init, and GPIO pin setup are now
     * handled inside uwb_bus_init() via the board config macros in
     * boards/Host/nxp/rt700_evk/uwb_bus_board_config.h.
     * No manual LPSPI16 clock/reset setup is required here. */

    UWBDemo_Init();

    /* starting ntf handling task*/
    demo_ntf_handling_task_start();

    testTaskHandle = uwb_demo_start();

}
