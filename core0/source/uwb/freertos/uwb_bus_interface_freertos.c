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
 * uwb_bus_interface_freertos.c  (board-agnostic shared implementation)
 *
 * FreeRTOS / MCUXpresso SDK port of the UWB SPI bus interface layer.
 * This single file supports both FRDM-RW612 and RT700 EVK (and any future
 * board) through compile-time selection controlled by the board config header.
 *
 * The board-specific header (uwb_bus_board_config.h) must define:
 *
 *   UWB_USE_LPSPI        0  ->  fsl_spi.h   (FlexComm SPI, e.g. RW612 SPI3)
 *                        1  ->  fsl_lpspi.h (LP_FLEXCOMM,  e.g. RT700 LPSPI1)
 *   UWB_SPI_MASTER_BASE  peripheral base pointer (SPI3 or LPSPI1)
 *   UWB_SPI_BAUDRATE     baud rate in Hz
 *   UWB_SPI_CLK_ATTACH   clock_attach_id_t for CLOCK_AttachClk()
 *   UWB_SPI_CLK_IDX      FlexComm/LP_FLEXCOMM index for clock freq query
 *   UWB_SPI_PCS_NUM      (LPSPI only) chip-select selector, e.g. kLPSPI_MasterPcs0
 *
 * SPI directional-byte protocol (same for both SoC families):
 *   0x00 = host write to SR250
 *   0xFF = host read  from SR250
 *
 * Non-blocking transfer model:
 *   Each transfer is started with the interrupt-driven non-blocking API
 *   (LPSPI_MasterTransferNonBlocking / SPI_MasterTransferNonBlocking).
 *   The transfer-complete callback signals mSpiTransferSem from ISR context.
 *   The calling FreeRTOS task blocks on phOsalUwb_ConsumeSemaphore_WithTimeout()
 *   for up to SPI_TRANSFER_TIMEOUT_MS milliseconds, then treats a timeout as
 *   a transfer failure.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fsl_clock.h"
#include "fsl_common.h"
#include "fsl_gpio.h"

/* Board-level pin/peripheral definitions - must come before SPI driver selection */
#include "uwb_bus_board_config.h"

/* Select the correct SPI driver header at compile time */
#if UWB_USE_LPSPI
#  include "fsl_lpspi.h"
#else
#  include "fsl_spi.h"
#endif

/* Peripheral reset driver - used when UWB_SPI_RESET_LINE is defined */
#include "fsl_reset.h"

/* UWB middleware headers */
#include "uwb_bus_interface.h"
#include "phNxpLogApis_TmlUwb.h"
#include "phOsalUwb.h"
#include "phUwb_BuildConfig.h"
#include "phUwbErrorCodes.h"
#include "uwb_logging.h"
#include "driver_config.h"
#include "phUwbTypes.h"

/* FreeRTOS */
#include "FreeRTOS.h"
#include "semphr.h"

/* -------------------------------------------------------------------------
 * Compile-time guard - board config must set UWB_USE_LPSPI
 * ------------------------------------------------------------------------- */
#ifndef UWB_USE_LPSPI
#  error "uwb_bus_board_config.h must define UWB_USE_LPSPI (0 or 1)"
#endif

/* Directional byte protocol */
#define DIRECTIONAL_BYTE_LEN        0x01U
#define DIRECTIONAL_BYTE_OFFSET     0x00U
#define DIRECTIONAL_BYTE_WRITE      0x00U
#define DIRECTIONAL_BYTE_READ       0xFFU

/* Maximum time to wait for a non-blocking transfer to complete (milliseconds). */
#define SPI_TRANSFER_TIMEOUT_MS     1500U

/* -------------------------------------------------------------------------
 * mSpiTransferSem  - signaled by the transfer-complete callback so that the
 * calling task can block until the non-blocking transfer finishes.
 * ------------------------------------------------------------------------- */
void *mSpiTransferSem = NULL;

/* Last transfer completion status captured inside the callback. */
static volatile status_t s_spiTransferStatus = kStatus_Fail;

/* -------------------------------------------------------------------------
 * SPI master handle - allocated statically because it must remain valid for
 * the lifetime of the driver and is accessed from ISR context.
 * ------------------------------------------------------------------------- */
#if UWB_USE_LPSPI
static lpspi_master_handle_t s_lpspiHandle;
#else
static spi_master_handle_t s_spiHandle;
#endif

/* =========================================================================
 * Transfer-complete callbacks
 *
 * Both callbacks capture the driver status and release mSpiTransferSem so
 * the waiting task can proceed.  phOsalUwb_ProduceSemaphore() detects ISR
 * context internally and calls xSemaphoreGiveFromISR() when appropriate.
 * =========================================================================
 *
 * spi_transfer_nonblocking (LPSPI path only)
 *
 * Initiates a non-blocking LPSPI transfer and blocks the calling FreeRTOS
 * task on mSpiTransferSem until the transfer-complete callback fires or the
 * timeout expires.  Returns kStatus_Success on success.
 *
 * For the FlexComm SPI path (UWB_USE_LPSPI == 0) each transfer function
 * manages the semaphore wait inline so this helper is not needed there.
 * ========================================================================= */

#if UWB_USE_LPSPI
static void lpspi_MasterCallback(LPSPI_Type *base,
                                 lpspi_master_handle_t *handle,
                                 status_t status,
                                 void *userData)
{
    (void)base;
    (void)handle;
    (void)userData;
    s_spiTransferStatus = status;
    (void)phOsalUwb_ProduceSemaphore(mSpiTransferSem);
}

static status_t spi_transfer_nonblocking(lpspi_transfer_t *pXfer)
{
    status_t status = LPSPI_MasterTransferNonBlocking(UWB_SPI_MASTER_BASE,
                                                      &s_lpspiHandle, pXfer);
    if (status != kStatus_Success)
    {
        LOG_E("spi_transfer_nonblocking: start failed 0x%x", (unsigned)status);
        return status;
    }

    /* Block until the callback signals completion or the timeout expires. */
    if (phOsalUwb_ConsumeSemaphore_WithTimeout(mSpiTransferSem, SPI_TRANSFER_TIMEOUT_MS)
            != UWBSTATUS_SUCCESS)
    {
        LOG_E("spi_transfer_nonblocking: transfer timeout");
        LPSPI_MasterTransferAbort(UWB_SPI_MASTER_BASE, &s_lpspiHandle);
        return kStatus_Timeout;
    }

    return s_spiTransferStatus;
}

#else /* FlexComm SPI */

static void spi_MasterCallback(SPI_Type *base,
                               spi_master_handle_t *handle,
                               status_t status,
                               void *userData)
{
    (void)base;
    (void)handle;
    (void)userData;
    s_spiTransferStatus = status;
    (void)phOsalUwb_ProduceSemaphore(mSpiTransferSem);
}

#endif /* UWB_USE_LPSPI */

/* =========================================================================
 * uwb_bus_init
 * ========================================================================= */

uwb_bus_status_t uwb_bus_init(uwb_bus_board_ctx_t *pCtx)
{
    if (pCtx == NULL)
    {
        NXPLOG_UWB_TML_E("uwbs bus context is NULL");
        return kUWB_bus_Status_FAILED;
    }

    /* Attach clock source for the SPI peripheral. */
    CLOCK_AttachClk(UWB_SPI_CLK_ATTACH);
#if defined(UWB_SPI_CLK_ATTACH2)
    CLOCK_AttachClk(UWB_SPI_CLK_ATTACH2);
#endif
#if defined(UWB_SPI_CLK_DIV) && defined(UWB_SPI_CLK_DIV_VALUE)
    CLOCK_SetClkDiv(UWB_SPI_CLK_DIV, UWB_SPI_CLK_DIV_VALUE);
#endif

#if defined(UWB_SPI_CLOCK_GATE)
    CLOCK_EnableClock(UWB_SPI_CLOCK_GATE);
#endif

#if defined(UWB_SPI_RESET_LINE)
    RESET_ClearPeripheralReset(UWB_SPI_RESET_LINE);
#endif

#if UWB_USE_LPSPI
    {
#if defined(UWB_LPFLEXCOMM_IDX)
        LP_FLEXCOMM_Init(UWB_LPFLEXCOMM_IDX, LP_FLEXCOMM_PERIPH_LPSPI);
#endif
        lpspi_master_config_t cfg;
        LPSPI_MasterGetDefaultConfig(&cfg);
        cfg.baudRate            = UWB_SPI_BAUDRATE;
        cfg.cpol                = kLPSPI_ClockPolarityActiveHigh;
        cfg.cpha                = kLPSPI_ClockPhaseFirstEdge;
        cfg.direction           = kLPSPI_MsbFirst;
        cfg.bitsPerFrame        = 8U;
        cfg.whichPcs            = UWB_SPI_PCS_NUM;
        cfg.pcsActiveHighOrLow  = kLPSPI_PcsActiveLow;
        cfg.enableInputDelay    = false;
        cfg.pinCfg              = kLPSPI_SdiInSdoOut;
        cfg.dataOutConfig       = kLpspiDataOutRetained;

        /* Adjust inter-frame delays to avoid CS glitches between chunks */
        cfg.pcsToSckDelayInNanoSec        = 1000000000U / (cfg.baudRate * 2U);
        cfg.lastSckToPcsDelayInNanoSec    = 1000000000U / (cfg.baudRate * 2U);
        cfg.betweenTransferDelayInNanoSec = 1000000000U / (cfg.baudRate * 2U);

#if defined(UWB_LPFLEXCOMM_IDX)
        LPSPI_MasterInit(UWB_SPI_MASTER_BASE, &cfg,
                         CLOCK_GetLPFlexCommClkFreq(UWB_SPI_CLK_IDX));
#else
        LPSPI_MasterInit(UWB_SPI_MASTER_BASE, &cfg,
                         CLOCK_GetLPSpiClkFreq(UWB_SPI_CLK_IDX));
#endif

        /* Register the interrupt-driven master handle and callback. */
        LPSPI_MasterTransferCreateHandle(UWB_SPI_MASTER_BASE, &s_lpspiHandle,
                                         lpspi_MasterCallback, NULL);

        /* Set the LPSPI IRQ priority to a value within the FreeRTOS managed
         * range.  Any ISR that calls a FreeRTOS API (xSemaphoreGiveFromISR)
         * must have a numeric ARM priority >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY.
         * The NVIC priority after reset is 0 (highest urgency), which triggers
         * the configASSERT in vPortValidateInterruptPriority().
         * UWB_SPI_IRQn and UWB_SPI_IRQ_PRIORITY are defined in uwb_bus_board_config.h. */
#if defined(UWB_SPI_IRQn) && defined(UWB_SPI_IRQ_PRIORITY)
        NVIC_SetPriority(UWB_SPI_IRQn, UWB_SPI_IRQ_PRIORITY);
#endif
    }
#else /* FlexComm SPI */
    {
        spi_master_config_t cfg;
        SPI_MasterGetDefaultConfig(&cfg);
        cfg.baudRate_Bps    = UWB_SPI_BAUDRATE;
        cfg.polarity        = kSPI_ClockPolarityActiveHigh;
        cfg.phase           = kSPI_ClockPhaseFirstEdge;
        cfg.direction       = kSPI_MsbFirst;
        cfg.dataWidth       = kSPI_Data8Bits;
        cfg.sselNum         = kSPI_Ssel0;
        cfg.sselPol         = kSPI_SpolActiveAllLow;
        cfg.enableLoopback  = false;

        SPI_MasterInit(UWB_SPI_MASTER_BASE, &cfg,
                       CLOCK_GetFlexCommClkFreq(UWB_SPI_CLK_IDX));

        /* Register the interrupt-driven master handle and callback. */
        SPI_MasterTransferCreateHandle(UWB_SPI_MASTER_BASE, &s_spiHandle,
                                       spi_MasterCallback, NULL);

        /* Set the FlexComm SPI IRQ priority into the FreeRTOS managed range.
         * Same requirement as the LPSPI path: the callback calls
         * xSemaphoreGiveFromISR(), which asserts if the priority is numerically
         * lower than configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY. */
#if defined(UWB_SPI_IRQn) && defined(UWB_SPI_IRQ_PRIORITY)
        NVIC_SetPriority(UWB_SPI_IRQn, UWB_SPI_IRQ_PRIORITY);
#endif
    }
#endif /* UWB_USE_LPSPI */

    /* Binary semaphore used to synchronise with the transfer-complete callback. */
    if (phOsalUwb_CreateBinSem(&mSpiTransferSem) != UWBSTATUS_SUCCESS)
    {
        LOG_E("uwb_bus_init: could not create mSpiTransferSem");
        return kUWB_bus_Status_FAILED;
    }

    if (phOsalUwb_CreateSemaphore(&pCtx->mIrqWaitSem, 0U) != UWBSTATUS_SUCCESS)
    {
        LOG_E("uwb_bus_init: could not create mIrqWaitSem");
        return kUWB_bus_Status_FAILED;
    }

    LOG_D("uwb_bus_init Done");
    return kUWB_bus_Status_OK;
}

/* =========================================================================
 * uwb_bus_data_tx  - write to SR250
 *
 * Sends one direction byte (0x00) followed by the full payload in a single
 * non-blocking SPI transfer.  The calling task blocks on mSpiTransferSem
 * until the transfer-complete ISR callback fires or the timeout expires.
 * ========================================================================= */

uwb_bus_status_t uwb_bus_data_tx(uwb_bus_board_ctx_t *pCtx,
                                   uint8_t *pBuf,
                                   size_t bufLen)
{
    status_t status;
    uwb_bus_status_t bus_status = kUWB_bus_Status_FAILED;

    if (pCtx == NULL) { NXPLOG_UWB_TML_E("uwbs bus context is NULL"); return kUWB_bus_Status_FAILED; }
    if (pBuf == NULL || bufLen == 0U) { return kUWB_bus_Status_FAILED; }

    /* Write direction byte into the slot reserved by the upper layer (pBuf[0]). */
    pBuf[DIRECTIONAL_BYTE_OFFSET] = DIRECTIONAL_BYTE_WRITE;

#if UWB_USE_LPSPI
    {
        lpspi_transfer_t xfer;
        xfer.txData      = &pBuf[DIRECTIONAL_BYTE_OFFSET];
        xfer.rxData      = NULL;
        xfer.dataSize    = bufLen + DIRECTIONAL_BYTE_LEN;
        xfer.configFlags = (uint32_t)UWB_SPI_PCS_NUM | (uint32_t)kLPSPI_MasterPcsContinuous |
                           (uint32_t)kLPSPI_MasterByteSwap;
        status = spi_transfer_nonblocking(&xfer);
        if (status == kStatus_Success)
        {
            bus_status = kUWB_bus_Status_OK;
        }
    }
#else /* FlexComm SPI */
    {
        spi_transfer_t xfer;
        xfer.txData      = &pBuf[DIRECTIONAL_BYTE_OFFSET];
        xfer.rxData      = NULL;
        xfer.dataSize    = bufLen + DIRECTIONAL_BYTE_LEN;
        xfer.configFlags = kSPI_FrameAssert;
        status = SPI_MasterTransferNonBlocking(UWB_SPI_MASTER_BASE, &s_spiHandle, &xfer);
        if (status == kStatus_Success)
        {
            if (phOsalUwb_ConsumeSemaphore_WithTimeout(mSpiTransferSem, SPI_TRANSFER_TIMEOUT_MS)
                    != UWBSTATUS_SUCCESS)
            {
                LOG_E("uwb_bus_data_tx: transfer timeout");
                SPI_MasterTransferAbort(UWB_SPI_MASTER_BASE, &s_spiHandle);
            }
            else if (s_spiTransferStatus == kStatus_Success)
            {
                bus_status = kUWB_bus_Status_OK;
            }
        }
        else
        {
            LOG_E("uwb_bus_data_tx: start failed 0x%x", (unsigned)status);
        }
    }
#endif /* UWB_USE_LPSPI */

    return bus_status;
}

/* =========================================================================
 * uwb_bus_data_rx  - read from SR250
 *
 * Sends the 0xFF direction byte from a local 1-byte TX buffer while
 * receiving the full response (direction byte echo + payload) into pBuf
 * in a single non-blocking transfer.  The calling task blocks on
 * mSpiTransferSem until the transfer-complete ISR callback fires or the
 * timeout expires.
 *
 * pBuf[0] receives the SR250 echo of the 0xFF direction byte.
 * SHIFT_AND_OVERRIDE_HEADER in the upper layer strips that byte.
 * ========================================================================= */

uwb_bus_status_t uwb_bus_data_rx(uwb_bus_board_ctx_t *pCtx,
                                   uint8_t *pBuf,
                                   size_t bufLen)
{
    status_t status;
    uwb_bus_status_t bus_status = kUWB_bus_Status_FAILED;
    /* One-byte TX buffer holding the direction byte; MOSI is don't-care after
     * the first byte from SR250's perspective, but the driver needs a TX
     * pointer for the full transfer length - it will repeat the last byte. */
    uint8_t txDirBuf[1] = {DIRECTIONAL_BYTE_READ};

    if (pCtx == NULL) { NXPLOG_UWB_TML_E("uwbs bus context is NULL"); return kUWB_bus_Status_FAILED; }
    if (pBuf == NULL || bufLen == 0U) { NXPLOG_UWB_TML_E("invalid params"); return kUWB_bus_Status_FAILED; }

#if UWB_USE_LPSPI
    {
        lpspi_transfer_t xfer;
        xfer.txData      = txDirBuf;
        xfer.rxData      = pBuf;
        xfer.dataSize    = bufLen + DIRECTIONAL_BYTE_LEN;
        xfer.configFlags = (uint32_t)UWB_SPI_PCS_NUM | (uint32_t)kLPSPI_MasterPcsContinuous |
                           (uint32_t)kLPSPI_MasterByteSwap;
        status = spi_transfer_nonblocking(&xfer);
        if (status == kStatus_Success)
        {
            if (*pBuf != 0xFFU)
            {
                LOG_E("Invalid directional byte in rx");
                LOG_AU8_E(pBuf, 5U);
                return kUWB_bus_Status_FAILED;
            }
            bus_status = kUWB_bus_Status_OK;
        }
        else
        {
            PRINTF("RX failed-0x%x\r\n", status);
        }
    }
#else /* FlexComm SPI */
    {
        spi_transfer_t xfer;
        xfer.txData      = txDirBuf;
        xfer.rxData      = pBuf;
        xfer.dataSize    = bufLen + DIRECTIONAL_BYTE_LEN;
        xfer.configFlags = kSPI_FrameAssert;
        status = SPI_MasterTransferNonBlocking(UWB_SPI_MASTER_BASE, &s_spiHandle, &xfer);
        if (status == kStatus_Success)
        {
            if (phOsalUwb_ConsumeSemaphore_WithTimeout(mSpiTransferSem, SPI_TRANSFER_TIMEOUT_MS)
                    != UWBSTATUS_SUCCESS)
            {
                LOG_E("uwb_bus_data_rx: transfer timeout");
                SPI_MasterTransferAbort(UWB_SPI_MASTER_BASE, &s_spiHandle);
            }
            else if (s_spiTransferStatus == kStatus_Success)
            {
                if (*pBuf != 0xFFU)
                {
                    LOG_E("Invalid directional byte in rx");
                    return kUWB_bus_Status_FAILED;
                }
                bus_status = kUWB_bus_Status_OK;
            }
        }
        else
        {
            LOG_E("uwb_bus_data_rx: start failed 0x%x", (unsigned)status);
        }
    }
#endif /* UWB_USE_LPSPI */

    return bus_status;
}

/* =========================================================================
 * uwb_bus_data_trx  - dispatch read or write
 *
 * For READ_MODE: sends the 0xFF direction byte via a local buffer and
 * receives the full response into pBuf (same as reference).
 * For WRITE_MODE: sends pBuf directly.
 * Both paths use the non-blocking API with semaphore synchronisation.
 * ========================================================================= */

uwb_bus_status_t uwb_bus_data_trx(uwb_bus_board_ctx_t *pCtx,
                                    uint8_t *pBuf,
                                    size_t bufLen)
{
    status_t status;
    uwb_bus_status_t bus_status = kUWB_bus_Status_FAILED;
    size_t data_size;
    uint8_t gTxDataBuff[2] = {0x00U, 0x00U};

    if (pCtx == NULL || pBuf == NULL || bufLen == 0U)
    {
        NXPLOG_UWB_TML_E("uwb_bus_data_trx: invalid params");
        return kUWB_bus_Status_FAILED;
    }

    data_size = bufLen + DIRECTIONAL_BYTE_LEN;

#if UWB_USE_LPSPI
    {
        lpspi_transfer_t xfer;
        xfer.dataSize    = data_size;
        xfer.configFlags = (uint32_t)UWB_SPI_PCS_NUM | (uint32_t)kLPSPI_MasterPcsContinuous |
                           (uint32_t)kLPSPI_MasterByteSwap;

        if (pCtx->op_mode == READ_MODE)
        {
            gTxDataBuff[DIRECTIONAL_BYTE_OFFSET] = DIRECTIONAL_BYTE_READ;
            xfer.txData = gTxDataBuff;
            xfer.rxData = pBuf;
            status = spi_transfer_nonblocking(&xfer);
            if (status == kStatus_Success)
            {
                if (*pBuf != 0xFFU)
                {
                    LOG_E("Invalid directional byte");
                    LOG_AU8_E(pBuf, 5U);
                    return kUWB_bus_Status_FAILED;
                }
                bus_status = kUWB_bus_Status_OK;
            }
            else
            {
                LOG_E("trx read error %d", __LINE__);
            }
        }
        else if (pCtx->op_mode == WRITE_MODE)
        {
            pBuf[DIRECTIONAL_BYTE_OFFSET] = DIRECTIONAL_BYTE_WRITE;
            xfer.rxData = NULL;
            xfer.txData = &pBuf[DIRECTIONAL_BYTE_OFFSET];
            status = spi_transfer_nonblocking(&xfer);
            if (status == kStatus_Success)
            {
                bus_status = kUWB_bus_Status_OK;
            }
            else
            {
                LOG_E("trx write error %d", __LINE__);
            }
        }
        else
        {
            LOG_E("uwb_bus_data_trx: invalid mode");
        }
    }
#else /* FlexComm SPI */
    {
        spi_transfer_t xfer;
        xfer.dataSize    = data_size;
        xfer.configFlags = kSPI_FrameAssert;

        if (pCtx->op_mode == READ_MODE)
        {
            gTxDataBuff[DIRECTIONAL_BYTE_OFFSET] = DIRECTIONAL_BYTE_READ;
            xfer.txData = gTxDataBuff;
            xfer.rxData = pBuf;
            status = SPI_MasterTransferNonBlocking(UWB_SPI_MASTER_BASE, &s_spiHandle, &xfer);
            if (status == kStatus_Success)
            {
                if (phOsalUwb_ConsumeSemaphore_WithTimeout(mSpiTransferSem, SPI_TRANSFER_TIMEOUT_MS)
                        != UWBSTATUS_SUCCESS)
                {
                    LOG_E("uwb_bus_data_trx: read timeout");
                    SPI_MasterTransferAbort(UWB_SPI_MASTER_BASE, &s_spiHandle);
                }
                else if (s_spiTransferStatus == kStatus_Success)
                {
                    bus_status = kUWB_bus_Status_OK;
                }
                else
                {
                    LOG_E("trx read error %d", __LINE__);
                }
            }
            else
            {
                LOG_E("trx read error %d", __LINE__);
            }
        }
        else if (pCtx->op_mode == WRITE_MODE)
        {
            pBuf[DIRECTIONAL_BYTE_OFFSET] = DIRECTIONAL_BYTE_WRITE;
            xfer.rxData = NULL;
            xfer.txData = &pBuf[DIRECTIONAL_BYTE_OFFSET];
            status = SPI_MasterTransferNonBlocking(UWB_SPI_MASTER_BASE, &s_spiHandle, &xfer);
            if (status == kStatus_Success)
            {
                if (phOsalUwb_ConsumeSemaphore_WithTimeout(mSpiTransferSem, SPI_TRANSFER_TIMEOUT_MS)
                        != UWBSTATUS_SUCCESS)
                {
                    LOG_E("uwb_bus_data_trx: write timeout");
                    SPI_MasterTransferAbort(UWB_SPI_MASTER_BASE, &s_spiHandle);
                }
                else if (s_spiTransferStatus == kStatus_Success)
                {
                    bus_status = kUWB_bus_Status_OK;
                }
                else
                {
                    LOG_E("trx write error %d", __LINE__);
                }
            }
            else
            {
                LOG_E("trx write error %d", __LINE__);
            }
        }
        else
        {
            LOG_E("uwb_bus_data_trx: invalid mode");
        }
    }
#endif /* UWB_USE_LPSPI */

    return bus_status;
}

/* =========================================================================
 * uwb_bus_deinit
 * ========================================================================= */

uwb_bus_status_t uwb_bus_deinit(uwb_bus_board_ctx_t *pCtx)
{
    if (pCtx == NULL) { LOG_E("uwbs bus context is NULL"); return kUWB_bus_Status_FAILED; }

#if UWB_USE_LPSPI
    LPSPI_MasterTransferAbort(UWB_SPI_MASTER_BASE, &s_lpspiHandle);
    LPSPI_Deinit(UWB_SPI_MASTER_BASE);
#else
    SPI_MasterTransferAbort(UWB_SPI_MASTER_BASE, &s_spiHandle);
    SPI_Deinit(UWB_SPI_MASTER_BASE);
#endif

    phOsalUwb_DeleteSemaphore(&mSpiTransferSem);
    phOsalUwb_DeleteSemaphore(&pCtx->mIrqWaitSem);

    mSpiTransferSem = NULL;
    phOsalUwb_SetMemory(pCtx, 0U, sizeof(uwb_bus_board_ctx_t));
    return kUWB_bus_Status_OK;
}

/* =========================================================================
 * uwb_bus_reset
 *
 * Toggle the RSTN GPIO to hard-reset the SR250, matching the reference
 * implementation from commit 58aca788.
 * ========================================================================= */

uwb_bus_status_t uwb_bus_reset(uwb_bus_board_ctx_t *pCtx)
{
    uwb_bus_io_val_set(pCtx, kUWBS_IO_O_RSTN, kUWBS_IO_State_Low);
    phOsalUwb_Delay(5U);
    uwb_bus_io_val_set(pCtx, kUWBS_IO_O_RSTN, kUWBS_IO_State_High);
    phOsalUwb_Delay(10U);
    return kUWB_bus_Status_OK;
}

/* =========================================================================
 * uwb_port_DelayinMicroSec  - microsecond busy-wait
 * ========================================================================= */

void uwb_port_DelayinMicroSec(int delay)
{
    /* Simple busy-loop matching the reference. Each iteration is roughly 6
     * cycles; adjust the multiplier if the core frequency changes. */
    volatile int cnt = 6 * delay;
    while (cnt--)
        ;
}
