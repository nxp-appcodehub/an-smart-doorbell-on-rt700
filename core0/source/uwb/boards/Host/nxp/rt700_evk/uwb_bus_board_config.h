/* Copyright 2024-2026 NXP
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
 * Board-specific hardware configuration for RT700-EVK B (MIMXRT798S) +
 * ETNA-TS-250-DevKit UWB module connected on Arduino headers.
 *
 * Included by the FreeRTOS common board driver files:
 *   freertos/uwb_bus_interface_freertos.c
 *   freertos/uwb_bus_io_freertos.c
 *
 * Hardware: ETNA-TS-250-DevKit (NXP Smart Doorbell UWB module)
 *           This module uses the same Arduino header pinout as the SR250-ARD
 *           development shield, so the same pin assignments apply.
 *
 * SPI:  LPSPI16 (standalone, NOT an LP_FLEXCOMM instance) on PORT2 pins 2-5.
 *       The pin mux function is FUNC1 (LPSPI16) for all four SPI signals.
 *   SOUT : PIO2_2  (Arduino D11 / MOSI)
 *   SCK  : PIO2_3  (Arduino D13 / SCK)
 *   SIN  : PIO2_4  (Arduino D12 / MISO)
 *   PCS0 : PIO2_5  (Arduino D10 / CS)
 *
 * GPIO: GPIO2 (BOARD_InitUwbPins configures GPIO_PortInit(GPIO2))
 *   RSTN -> GPIO2, pin 9  (PIO2_9,  Arduino D8 - active-low reset to UWB module)
 *   IRQ  -> GPIO2, pin 14 (PIO2_14, Arduino D9 - rising-edge IRQ from UWB module)
 *
 * GPIO interrupt vector:
 *   GPIO20_IRQHandler covers GPIO2 pins 0-31 (bank 0).
 *
 * Clock:
 *   LPSPI16 has its own direct clock mux (not through LP_FLEXCOMM).
 *   Use CLOCK_AttachClk(kFRO1_DIV1_to_LPSPI16) then
 *   CLOCK_GetLPSpiClkFreq(16U) to query the resulting frequency.
 *   No LP_FLEXCOMM_Init() call is needed for LPSPI16.
 *
 * IMPORTANT: The IOPCTL pin mux for the above pins must be configured by
 * BOARD_InitUwbPins() (pin_mux.c) BEFORE calling uwb_bus_init().
 */

#ifndef UWB_BUS_BOARD_CONFIG_H
#define UWB_BUS_BOARD_CONFIG_H

#include "fsl_clock.h"
#include "fsl_gpio.h"
#include "fsl_lpspi.h"

/* --- SPI configuration -------------------------------------------------- */

/* Use fsl_lpspi.h (standalone LPSPI16, not LP_FLEXCOMM) */
#define UWB_USE_LPSPI               (1)

/* LPSPI16 peripheral base pointer */
#define UWB_SPI_MASTER_BASE         LPSPI16

/* UWB_LPFLEXCOMM_IDX is intentionally NOT defined here.
 * LPSPI16 is a standalone LPSPI peripheral on RT798 and does not go through
 * the LP_FLEXCOMM mux.  The LP_FLEXCOMM_Init() call in uwb_bus_init() is
 * compiled out when this macro is absent. */

/* PCS line used for chip-select */
#define UWB_SPI_PCS_NUM             kLPSPI_MasterPcs0

/* SPI clock: LPSPI16 has a single direct clock mux on RT700.
 * Attach FRO1_DIV1 to LPSPI16 then divide by 8:
 *   FRO1 = 192 MHz, /8 = 24 MHz source -> 8 MHz SPI baud rate.
 * This matches the setup that worked in commit 58aca78 (kFRO1_DIV1_to_LPSPI16
 * + CLOCK_SetClkDiv(kCLOCK_DivLpspi16Clk, 8)).
 * UWB_SPI_CLK_ATTACH2 is intentionally NOT defined (single step). */
#define UWB_SPI_CLK_ATTACH          kFRO1_DIV1_to_LPSPI16

/* Divider for the LPSPI16 source clock.
 * Without this the divider register retains its reset value and
 * LPSPI_MasterInit() programs an incorrect baud rate. */
#define UWB_SPI_CLK_DIV             kCLOCK_DivLpspi16Clk
#define UWB_SPI_CLK_DIV_VALUE       (8U)

/* LPSPI16 clock ID for CLOCK_GetLPSpiClkFreq(id) */
#define UWB_SPI_CLK_IDX             (16U)

/* SPI baudrate: 8 MHz (ETNA-TS-250-DevKit / SR250 supports up to 10 MHz) */
#define UWB_SPI_BAUDRATE            (8000000U)

/* Maximum wait time for one SPI transfer to complete (ms) */
#define UWB_SPI_TRANSFER_TIMEOUT_MS (1500U)
/* MAX_UWBS_SPI_TRANSFER_TIMEOUT is defined in uwb_bus_interface.h - do not
 * redefine here to avoid a -Werror redefinition error. */

/* --- GPIO configuration -------------------------------------------------- */

/* RSTN pin: GPIO2, pin 9 (PIO2_9, Arduino D8)
 * Active-low reset output from host to UWB module. */
#define UWB_RSTN_GPIO_BASE          GPIO2
#define UWB_RSTN_GPIO_PORT_IDX      (2U)
#define UWB_RSTN_GPIO_PIN           (9U)

/* IRQ pin: GPIO2, pin 14 (PIO2_14, Arduino D9)
 * Rising-edge interrupt input from UWB module to host. */
#define UWB_IRQ_GPIO_BASE           GPIO2
#define UWB_IRQ_GPIO_PORT_IDX       (2U)
#define UWB_IRQ_GPIO_PIN            (14U)

/* GPIO interrupt source and ISR name.
 * GPIO20_IRQn / GPIO20_IRQHandler covers GPIO2 pins 0-31 (bank 0). */
#define UWB_GPIO_IRQ                GPIO20_IRQn
#define UWB_GPIO_IRQ_HANDLER        GPIO20_IRQHandler

/* Interrupt priorities for FreeRTOS API safety.
 * Any ISR that calls a FreeRTOS API (e.g. xSemaphoreGiveFromISR) must have
 * a numeric ARM priority >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY (2).
 * Default NVIC priority after reset is 0 (highest), which violates the
 * FreeRTOS vPortValidateInterruptPriority() check.
 * Keep these values in sync with CONFIG_configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY
 * in core0/prj.conf (currently 2). */
#define UWB_GPIO_IRQ_PRIORITY       (2U)   /* must be >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY */
#define UWB_SPI_IRQn                LPSPI16_IRQn
#define UWB_SPI_IRQ_PRIORITY        (2U)   /* must be >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY */

/* --- Clock gate and reset ------------------------------------------------- */

/* Enable LPSPI16 clock gate before LPSPI_MasterInit().
 * Without this CLOCK_GetLPSpiClkFreq(16) returns 0 and every SPI transfer
 * times out, blocking UwbApi_Initialize() indefinitely in StandaloneTask.
 * The enum is from fsl_clock.h for MIMXRT798S:
 *   kCLOCK_LPSpi16 = CLK_GATE_DEFINE(CLK_CTL4_PSCCTL0, 5) */
#define UWB_SPI_CLOCK_GATE          kCLOCK_LPSpi16

/* Deassert the LPSPI16 peripheral reset before init.
 * Enum from fsl_reset.h: kLPSPI16_RST_SHIFT_RSTn = (RST_CTL4_PSCCTL0 << 8) | 14U */
#define UWB_SPI_RESET_LINE          kLPSPI16_RST_SHIFT_RSTn

/* --- Timer queue ---------------------------------------------------------- */
#define UWB_TIMER_QUEUE_LENGTH      (10U)

#endif /* UWB_BUS_BOARD_CONFIG_H */
