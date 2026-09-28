/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _APP_H_
#define _APP_H_

#include <stdint.h>

/*******************************************************************************
 * Definitions
 ******************************************************************************/
/*${macro:start}*/
/* Address of memory, from which the secondary core will boot */
#define CORE1_BOOT_ADDRESS 0x20600000
#define APP_INVALIDATE_CACHE_FOR_SECONDARY_CORE_IMAGE_MEMORY

#if defined(__CC_ARM) || defined(__ARMCC_VERSION)
extern uint32_t Image$$CORE1_REGION$$Base;
extern uint32_t Image$$CORE1_REGION$$Length;
#define CORE1_IMAGE_START &Image$$CORE1_REGION$$Base
#elif defined(__ICCARM__)
#pragma section = "__core1_image"
#define CORE1_IMAGE_START __section_begin("__core1_image")
#elif (defined(__GNUC__)) && (!defined(__MCUXPRESSO))
extern const char core1_image_start[];
extern const char *core1_image_end;
extern uint32_t core1_image_size;
#define CORE1_IMAGE_START ((void *)core1_image_start)
#define CORE1_IMAGE_SIZE  ((void *)core1_image_size)
#endif

/* Definition for switch 5(user1 button) */
#define BOARD_SW_GPIO_IRQ_PRIORITY (5U)
#define BOARD_SW_GPIO        GPIO0
#define BOARD_SW_GPIO_PIN    9U
#define BOARD_SW_IRQ         GPIO00_IRQn
#define BOARD_SW_IRQ_HANDLER GPIO00_IRQHandler
#define BOARD_SW_NAME        "SW5"


/* Definition for audio and codec */
#define DEMO_SAI                       SAI0
#define DEMO_SAI_CHANNEL               (0)
#define DEMO_SAI_TX_SYNC_MODE          kSAI_ModeAsync
#define DEMO_SAI_RX_SYNC_MODE          kSAI_ModeSync
#define DEMO_SAI_TX_BIT_CLOCK_POLARITY kSAI_PolarityActiveLow
#define DEMO_SAI_MASTER_SLAVE          kSAI_Master
#define DEMO_AUDIO_DATA_CHANNEL        (2U)
#define DEMO_AUDIO_BIT_WIDTH           kSAI_WordWidth16bits
#define DEMO_AUDIO_SAMPLE_RATE         (kSAI_SampleRate8KHz/*kSAI_SampleRate16KHz*/)
#define DEMO_AUDIO_MASTER_CLOCK        DEMO_SAI_CLK_FREQ

#define DEMO_DMA                 DMA0
#define DEMO_TX_EDMA_CHANNEL     14U
#define DEMO_RX_EDMA_CHANNEL     15U
#define DEMO_SAI_TX_EDMA_CHANNEL kDmaRequestMuxSai0Tx
#define DEMO_SAI_RX_EDMA_CHANNEL kDmaRequestMuxSai0Rx

/* Get frequency of sai clock */
#define DEMO_SAI_CLK_FREQ CLOCK_GetSaiClkFreq()

/* Get frequency of lpi2c clock */
#define DEMO_I2C_CLK_FREQ 24000000U /* CLOCK_GetLPI2cClkFreq(2) */
#define BOARD_SAI_RXCONFIG(config, mode)
#define DEMO_QUICKACCESS_SECTION_CACHEABLE 1U

#if defined(__cplusplus)
extern "C" {
#endif /* __cplusplus */

/*******************************************************************************
 * Prototypes
 ******************************************************************************/
/*${prototype:start}*/
void BOARD_InitHardware(void);
/*${prototype:end}*/


#ifdef CORE1_IMAGE_COPY_TO_RAM
uint32_t get_core1_image_size(void);
#endif /* CORE1_IMAGE_COPY_TO_RAM */

#ifdef APP_INVALIDATE_CACHE_FOR_SECONDARY_CORE_IMAGE_MEMORY
void invalidate_cache_for_core1_image_memory(uint32_t address, uint32_t size_byte);
#endif /* APP_INVALIDATE_CACHE_FOR_SECONDARY_CORE_IMAGE_MEMORY */
/*${prototype:end}*/

#if defined(__cplusplus)
}
#endif /* __cplusplus */

#endif /* __APP_H__ */
