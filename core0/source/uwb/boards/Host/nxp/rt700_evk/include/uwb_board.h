/* Copyright 2024 NXP
 *
 * NXP Confidential. This software is owned or controlled by NXP and may only
 * be used strictly in accordance with the applicable license terms.  By
 * expressly accepting such terms or by downloading, installing, activating
 * and/or otherwise using the software, you are agreeing that you have read,
 * and that you agree to comply with and are bound by, such license terms.  If
 * you do not agree to be bound by the applicable license terms, then you may
 * not retain, install, activate or otherwise use the software.
 */

#pragma once

#include "board.h"
#include "pin_mux.h"
#include "clock_config.h"
#include <uwb_board_values.h>
#include "phUwb_BuildConfig.h"
#include "fsl_debug_console.h"
#include "fsl_common.h"

/* PRINTF is required by uwb_logging.h before it is included.
 * fsl_debug_console.h defines it as DbgConsole_Printf on NXP SDK targets. */
#ifndef PRINTF
#define PRINTF DbgConsole_Printf
#endif

/* AddDelayInMicroSec - busy-wait helper used by the TML transport layer
 * and the pnp SPI driver interface.  Maps to SDK_DelayAtLeastUs. */
static inline void AddDelayInMicroSec(int delay)
{
    SDK_DelayAtLeastUs((uint32_t)delay, SDK_DEVICE_MAXIMUM_CPU_CLOCK_FREQUENCY);
}

#define phPlatform_Is_Irq_Context() (SCB->ICSR & SCB_ICSR_VECTACTIVE_Msk)

#define UWB_BOARD_ENABLE_FW_DOWNLOAD_ON_UWBINIT 1

#define UWB_BOARD_ENABLE_EXT_FLASH_BASED_FW_DOWNLOAD 0

#define UWB_BOARD_RX_ANTENNA_CONFIG_MODE_VAL UWB_BOARD_RX_ANTENNA_CONFIG_MODE_2DAOA

#define UWB_BOARD_DEFINE_LED_APIS(THE_LED)                      \
    static inline void UWB_BOARD_GPIO_SET_##THE_LED##_ON(void)  \
    {                                                           \
    }                                                           \
    static inline void UWB_BOARD_GPIO_SET_##THE_LED##_OFF(void) \
    {                                                           \
    }

UWB_BOARD_DEFINE_LED_APIS(LED_R)
UWB_BOARD_DEFINE_LED_APIS(LED_O)
UWB_BOARD_DEFINE_LED_APIS(LED_B)
UWB_BOARD_DEFINE_LED_APIS(LED_G)

/**
 * @brief Board initialization
 *
 */
void board_init(void);
