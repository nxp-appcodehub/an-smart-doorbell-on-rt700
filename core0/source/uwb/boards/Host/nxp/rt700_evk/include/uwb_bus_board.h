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

#ifndef __UWB_BUS_BOARD_H__
#define __UWB_BUS_BOARD_H__

#include <uwb_uwbs_tml_io.h>
#include "board.h"

/** Board Specific BUS Interface for the Host HAL */
typedef enum
{
    WRITE_MODE = 0,
    READ_MODE
} op_mode_t;

typedef struct
{
    op_mode_t op_mode;
    /* Handle for the SPI master driver (lpspi_master_handle_t or spi_master_handle_t) */
    void *masterHandle;
    /* This semaphore is used to wait for read interrupt from helios */
    void *mIrqWaitSem;
} uwb_bus_board_ctx_t;

#endif /* __UWB_BUS_BOARD_H__ */
