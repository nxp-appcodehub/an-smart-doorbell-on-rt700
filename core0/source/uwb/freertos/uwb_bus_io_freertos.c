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
 * uwb_bus_io_freertos.c  (board-agnostic shared implementation)
 *
 * FreeRTOS / MCUXpresso SDK port of the UWB GPIO and IRQ IO layer.
 * This single file supports FRDM-RW612, RT700 EVK, and any future board
 * through macros supplied by the board-specific uwb_bus_board_config.h.
 *
 * Required macros in uwb_bus_board_config.h:
 *
 *   UWB_RSTN_GPIO_BASE      GPIO controller base for RSTN pin
 *   UWB_RSTN_GPIO_PORT_IDX  Port index passed to GPIO_PinInit / GPIO_PinWrite
 *   UWB_RSTN_GPIO_PIN       Pin number for RSTN
 *   UWB_IRQ_GPIO_BASE       GPIO controller base for IRQ pin
 *   UWB_IRQ_GPIO_PORT_IDX   Port index for IRQ pin
 *   UWB_IRQ_GPIO_PIN        Pin number for IRQ
 *   UWB_GPIO_IRQ            IRQn_Type for the GPIO interrupt (e.g. GPIO_INTA_IRQn)
 *   UWB_GPIO_IRQ_HANDLER    ISR function name matching the vector table entry
 *                           (e.g. GPIO_INTA_IRQHandler or GPIO4_IRQHandler)
 *
 * The GPIO API used (GPIO_PinInit, GPIO_PinWrite, GPIO_PinRead,
 * GPIO_SetPinInterruptConfig, GPIO_PinClearInterruptFlag) is the same across
 * both RW612 and RT700 in the MCUXpresso SDK - only the peripheral base
 * pointer and port index differ.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "fsl_gpio.h"
#include "fsl_common.h"

#include "uwb_bus_board_config.h"
#include "uwb_bus_interface.h"
#include "phNxpLogApis_Board.h"
#include "phOsalUwb.h"

/* -------------------------------------------------------------------------
 * Compile-time guards
 * ------------------------------------------------------------------------- */
#ifndef UWB_GPIO_IRQ_HANDLER
#  error "uwb_bus_board_config.h must define UWB_GPIO_IRQ_HANDLER"
#endif
#ifndef UWB_GPIO_IRQ
#  error "uwb_bus_board_config.h must define UWB_GPIO_IRQ"
#endif

#define UWB_NUM_INTERRUPTS  1U

/* -------------------------------------------------------------------------
 * Module state
 * ------------------------------------------------------------------------- */
static uwbs_io_callback s_mCallbacks[UWB_NUM_INTERRUPTS];

/* -------------------------------------------------------------------------
 * Board-agnostic UWB IRQ dispatcher.
 * Clears the hardware flag, then calls the registered callback.
 * Named UWB_HELIOS_IRQ_HANDLER so it can be called from the SDK ISR below.
 * ------------------------------------------------------------------------- */
static void UWB_HELIOS_IRQ_HANDLER(void)
{
    GPIO_PinClearInterruptFlag(UWB_IRQ_GPIO_BASE,
                               UWB_IRQ_GPIO_PIN);

    if (s_mCallbacks[0].fn != NULL)
    {
        s_mCallbacks[0].fn(s_mCallbacks[0].args);
    }
}

/*
 * SDK vector table ISR.  The name is provided by the board config so it
 * matches the exact entry in the NVIC vector table for the target SoC.
 *
 * Example:
 *   RW612  ->  UWB_GPIO_IRQ_HANDLER = GPIO_INTA_IRQHandler
 *   RT700  ->  UWB_GPIO_IRQ_HANDLER = GPIO4_IRQHandler
 */
void UWB_GPIO_IRQ_HANDLER(void)
{
    UWB_HELIOS_IRQ_HANDLER();
    SDK_ISR_EXIT_BARRIER;
}

/* -------------------------------------------------------------------------
 * IRQ callback: signal the TML read task.
 * Runs in ISR context (called from UWB_HELIOS_IRQ_HANDLER).
 * ------------------------------------------------------------------------- */
void uwb_bus_io_irq_cb(void *args)
{
    uwb_bus_board_ctx_t *pCtx = (uwb_bus_board_ctx_t *)args;
    (void)phOsalUwb_ProduceSemaphore(pCtx->mIrqWaitSem);
}

/* =========================================================================
 * uwb_bus_io_deinit
 * ========================================================================= */

uwb_bus_status_t uwb_bus_io_deinit(uwb_bus_board_ctx_t *pCtx)
{
    (void)pCtx;
    DisableIRQ(UWB_GPIO_IRQ);
    memset(s_mCallbacks, 0, sizeof(s_mCallbacks));
    return kUWB_bus_Status_OK;
}

/* =========================================================================
 * uwb_bus_io_init
 * ========================================================================= */

uwb_bus_status_t uwb_bus_io_init(uwb_bus_board_ctx_t *pCtx)
{
    gpio_pin_config_t pinCfg;

    if (pCtx == NULL)
    {
        LOG_E("uwbs bus context is NULL");
        return kUWB_bus_Status_FAILED;
    }

    /* RSTN: output, initially low (SR250 held in reset) */
    pinCfg.pinDirection = kGPIO_DigitalOutput;
    pinCfg.outputLogic  = 0U;
    GPIO_PinInit(UWB_RSTN_GPIO_BASE, UWB_RSTN_GPIO_PIN, &pinCfg);

    return uwb_bus_io_uwbs_irq_enable(pCtx);
}

/* =========================================================================
 * uwb_bus_io_val_set
 * ========================================================================= */

uwb_bus_status_t uwb_bus_io_val_set(uwb_bus_board_ctx_t *pCtx,
                                      uwbs_io_t gpioPin,
                                      uwbs_io_state_t gpioValue)
{
    if (pCtx == NULL)       { LOG_E("uwbs bus context is NULL"); return kUWB_bus_Status_FAILED; }
    if (gpioValue == kUWBS_IO_State_NA) { LOG_E("Invalid GPIO state"); return kUWB_bus_Status_FAILED; }

    switch (gpioPin)
    {
    case kUWBS_IO_O_RSTN:
        GPIO_PinWrite(UWB_RSTN_GPIO_BASE, UWB_RSTN_GPIO_PIN, (uint8_t)gpioValue);
        return kUWB_bus_Status_OK;
    default:
        LOG_E("UWBD IO GPIO Pin not supported");
        return kUWB_bus_Status_FAILED;
    }
}

/* =========================================================================
 * uwb_bus_io_val_get
 * ========================================================================= */

uwb_bus_status_t uwb_bus_io_val_get(uwb_bus_board_ctx_t *pCtx,
                                      uwbs_io_t gpioPin,
                                      uwbs_io_state_t *pGpioValue)
{
    if (pCtx == NULL || pGpioValue == NULL)
    {
        LOG_E("uwbs bus context or value pointer is NULL");
        return kUWB_bus_Status_FAILED;
    }

    switch (gpioPin)
    {
    case kUWBS_IO_I_UWBS_IRQ:
        *pGpioValue = (uwbs_io_state_t)GPIO_PinRead(UWB_IRQ_GPIO_BASE,
                                                     UWB_IRQ_GPIO_PIN);
        break;
    default:
        LOG_E("UWBD IO GPIO Pin not supported");
        return kUWB_bus_Status_FAILED;
    }

    return kUWB_bus_Status_OK;
}

/* =========================================================================
 * uwb_bus_io_irq_dis
 * ========================================================================= */

uwb_bus_status_t uwb_bus_io_irq_dis(uwb_bus_board_ctx_t *pCtx, uwbs_io_t irqPin)
{
    if (pCtx == NULL) { LOG_E("uwbs bus context is NULL"); return kUWB_bus_Status_FAILED; }

    switch (irqPin)
    {
    case kUWBS_IO_I_UWBS_IRQ: break;
    default:
        LOG_E("UWBD IO GPIO Pin Interrupt not supported");
        return kUWB_bus_Status_FAILED;
    }

    /* Clear any pending interrupt flag, disable edge detection, then mask at NVIC.
     * Matches the reference Host/uwb_bus_io.c sequence. */
    GPIO_PinClearInterruptFlag(UWB_IRQ_GPIO_BASE, UWB_IRQ_GPIO_PIN);
    GPIO_SetPinInterruptConfig(UWB_IRQ_GPIO_BASE, UWB_IRQ_GPIO_PIN,
                               kGPIO_InterruptStatusFlagDisabled);
    DisableIRQ(UWB_GPIO_IRQ);

    s_mCallbacks[0].fn   = NULL;
    s_mCallbacks[0].args = NULL;
    return kUWB_bus_Status_OK;
}

/* =========================================================================
 * uwb_bus_io_irq_en
 * ========================================================================= */

uwb_bus_status_t uwb_bus_io_irq_en(uwb_bus_board_ctx_t *pCtx,
                                     uwbs_io_t irqPin,
                                     uwbs_io_callback *pCallback)
{
    gpio_pin_config_t pinCfg;

    if (pCtx == NULL || pCallback == NULL)
    {
        LOG_E("uwbs bus context or callback is NULL");
        return kUWB_bus_Status_FAILED;
    }

    switch (irqPin)
    {
    case kUWBS_IO_I_UWBS_IRQ:
        s_mCallbacks[0].fn   = pCallback->fn;
        s_mCallbacks[0].args = pCallback->args;
        break;
    default:
        LOG_E("UWBD IO GPIO Pin Interrupt not supported");
        return kUWB_bus_Status_FAILED;
    }

    /* IRQ pin: digital input */
    pinCfg.pinDirection = kGPIO_DigitalInput;
    pinCfg.outputLogic  = 0U;
    GPIO_PinInit(UWB_IRQ_GPIO_BASE, UWB_IRQ_GPIO_PIN, &pinCfg);

    /* Rising-edge interrupt */
    GPIO_SetPinInterruptConfig(UWB_IRQ_GPIO_BASE, UWB_IRQ_GPIO_PIN,
                               kGPIO_InterruptRisingEdge);

    /* Set priority before enabling: must be >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY
     * so that phOsalUwb_ProduceSemaphore() (called from the ISR) does not
     * trigger the FreeRTOS vPortValidateInterruptPriority() assertion. */
#if defined(UWB_GPIO_IRQ_PRIORITY)
    EnableIRQWithPriority(UWB_GPIO_IRQ, UWB_GPIO_IRQ_PRIORITY);
#else
    EnableIRQ(UWB_GPIO_IRQ);
#endif
    return kUWB_bus_Status_OK;
}

/* =========================================================================
 * uwb_bus_io_irq_wait
 * ========================================================================= */

uwb_bus_status_t uwb_bus_io_irq_wait(uwb_bus_board_ctx_t *pCtx,
                                       uint32_t timeout_ms)
{
    if (pCtx == NULL) { LOG_E("uwbs bus context is NULL"); return kUWB_bus_Status_FAILED; }

    if (phOsalUwb_ConsumeSemaphore_WithTimeout(pCtx->mIrqWaitSem, timeout_ms)
        != UWBSTATUS_SUCCESS)
    {
        return kUWB_bus_Status_FAILED;
    }

    return kUWB_bus_Status_OK;
}

/* =========================================================================
 * uwb_bus_io_uwbs_irq_enable
 * ========================================================================= */

uwb_bus_status_t uwb_bus_io_uwbs_irq_enable(uwb_bus_board_ctx_t *pCtx)
{
    uwbs_io_callback cb;

    if (pCtx == NULL) { LOG_E("uwbs bus context is NULL"); return kUWB_bus_Status_FAILED; }

    cb.fn   = uwb_bus_io_irq_cb;
    cb.args = pCtx;

    return uwb_bus_io_irq_en(pCtx, kUWBS_IO_I_UWBS_IRQ, &cb);
}
