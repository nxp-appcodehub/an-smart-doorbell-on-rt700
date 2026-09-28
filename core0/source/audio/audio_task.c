/*
 * Copyright (c) 2015, Freescale Semiconductor, Inc.
 * Copyright 2016-2020 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include "app.h"
#include "board.h"
#include "fsl_debug_console.h"
#include "fsl_sai_edma.h"
#include "fsl_codec_common.h"

#include "fsl_wm8962.h"
#include "fsl_codec_adapter.h"
#include "fsl_clock.h"
#include "fsl_sai.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "fsl_edma_soc.h"
#include "fsl_cache.h"
#include "app_msg.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define BUFFER_SIZE   (640UL)
#define BUFFER_NUMBER_RX (4U)
#define BUFFER_NUMBER_TX (2U)
#ifndef DEMO_CODEC_VOLUME
#define DEMO_CODEC_VOLUME 80UL
#endif
/*******************************************************************************
 * Prototypes
 ******************************************************************************/

/*******************************************************************************
 * Variables
 ******************************************************************************/
static uint8_t g_AudioRXBuffer[BUFFER_NUMBER_RX * BUFFER_SIZE] __attribute__((aligned(4)));

//ping-pong buffer for audio playing
static uint8_t g_AudioTXBuffer[BUFFER_SIZE * BUFFER_NUMBER_TX] __attribute__((aligned(4)));

#if defined(DEMO_QUICKACCESS_SECTION_CACHEABLE) && DEMO_QUICKACCESS_SECTION_CACHEABLE
AT_NONCACHEABLE_SECTION(sai_edma_handle_t txHandle);
AT_NONCACHEABLE_SECTION(sai_edma_handle_t rxHandle);
#else
AT_QUICKACCESS_SECTION_DATA(sai_edma_handle_t txHandle);
AT_QUICKACCESS_SECTION_DATA(sai_edma_handle_t rxHandle);
#endif
edma_handle_t dmaTxHandle = {0}, dmaRxHandle = {0};
extern codec_config_t boardCodecConfig;
codec_handle_t codecHandle;
#if (defined(DEMO_EDMA_HAS_CHANNEL_CONFIG) && DEMO_EDMA_HAS_CHANNEL_CONFIG)
extern edma_config_t dmaConfig;
#else
edma_config_t dmaConfig = {0};
#endif

#define audio_task_STACK_DEPTH (512UL)
static QueueHandle_t s_AudioTaskQueue = NULL;
// Unified callback tracking for RX
static audio_data_cb_t g_audio_rx_cb = NULL;
static void* g_audio_rx_cb_para = NULL;

// Unified callback tracking for TX
static volatile audio_data_cb_t g_audio_tx_cb = NULL;
static void* g_audio_tx_cb_para = NULL;
static volatile uint8_t g_audio_tx_cur_xfer_idx = 0U;

static volatile bool g_audio_codec_ready = false;


static void task_audio(void *param);
void audio_task_notify(uint8_t cmd, msg_t* para);

/*******************************************************************************
 * Code
 ******************************************************************************/

 /*${variable:start}*/
wm8962_config_t wm8962Config = {
    .i2cConfig = {.codecI2CInstance = BOARD_CODEC_I2C_INSTANCE, .codecI2CSourceClock = DEMO_I2C_CLK_FREQ},
    .route =
        {
            .enableLoopBack            = false,
            .leftInputPGASource        = kWM8962_InputPGASourceInput1,
            .leftInputMixerSource      = kWM8962_InputMixerSourceInputPGA,
            .rightInputPGASource       = kWM8962_InputPGASourceInput3,
            .rightInputMixerSource     = kWM8962_InputMixerSourceInputPGA,
            .leftHeadphoneMixerSource  = kWM8962_OutputMixerDisabled,
            .leftHeadphonePGASource    = kWM8962_OutputPGASourceDAC,
            .rightHeadphoneMixerSource = kWM8962_OutputMixerDisabled,
            .rightHeadphonePGASource   = kWM8962_OutputPGASourceDAC,
        },
    .slaveAddress = WM8962_I2C_ADDR,
    .bus          = kWM8962_BusI2S,
    .format       = {.mclk_HZ    = 24576000U,
                     .sampleRate = kWM8962_AudioSampleRate8KHz,//kWM8962_AudioSampleRate16KHz,
                     .bitWidth   = kWM8962_AudioBitWidth16bit},
    .masterSlave  = false,
};
codec_config_t boardCodecConfig = {.codecDevType = kCODEC_WM8962, .codecDevConfig = &wm8962Config};

sai_master_clock_t mclkConfig;

/*${variable:end}*/

/*${function:start}*/
static void AUDIO_MASTER_CLOCK_CONFIG(void)
{
    mclkConfig.mclkOutputEnable = true;
    mclkConfig.mclkHz           = 24576000U;
    mclkConfig.mclkSourceClkHz  = 24576000U;
    SAI_SetMasterClockConfig(DEMO_SAI, &mclkConfig);
}

static void Audio_InitHardware(void)
{
    /*Clock setting for LPI2C */
    CLOCK_AttachClk(kFCCLK0_to_FLEXCOMM2);

    /* SAI clock 368.64 / 15 = 24.576MHz */
    CLOCK_AttachClk(kAUDIO_PLL_PFD3_to_AUDIO_VDD2);
    CLOCK_AttachClk(kAUDIO_VDD2_to_SAI012);
    CLOCK_SetClkDiv(kCLOCK_DivSai012Clk, 15U);

    RESET_ClearPeripheralReset(kSAI0_RST_SHIFT_RSTn);
    EDMA_EnableRequest(DEMO_DMA, DEMO_SAI_TX_EDMA_CHANNEL);
    EDMA_EnableRequest(DEMO_DMA, DEMO_SAI_RX_EDMA_CHANNEL);

    SYSCON0->SAI0_MCLK_CTRL |= SYSCON0_SAI0_MCLK_CTRL_SAIMCLKDIR_MASK;
}


static void rx_callback(I2S_Type *base, sai_edma_handle_t *handle, status_t status, void *userData)
{
    if (kStatus_SAI_RxError == status)
    {
        /* Handle the error. */
        PRINTF("SAI rx error!\n\r");

    }
    else if (g_audio_rx_cb != NULL && g_audio_rx_cb_para != NULL)
    {
        uint32_t cur_idx = *((uint32_t *)userData);
        uint8_t* buf = g_AudioRXBuffer + cur_idx * BUFFER_SIZE;

        XCACHE_InvalidateCacheByRange((uint32_t)buf, BUFFER_SIZE);
        g_audio_rx_cb(buf, BUFFER_SIZE, g_audio_rx_cb_para);

        *((uint32_t *)userData) = (cur_idx + 1) % BUFFER_NUMBER_RX;
    }
}

static void tx_callback(I2S_Type *base, sai_edma_handle_t *handle, status_t status, void *userData)
{
    uint8_t* completed_buf = &g_AudioTXBuffer[g_audio_tx_cur_xfer_idx * BUFFER_SIZE];

    if (kStatus_SAI_TxError == status)
    {
        /* Handle the error. */
        PRINTF("SAI tx error:%d\n\r", status);
        return;
    }

    //it is time to fill the cur xfer buffer as it has been finished
    if (g_audio_tx_cb != NULL)
    {

        g_audio_tx_cb(completed_buf, BUFFER_SIZE, g_audio_tx_cb_para);

        //make sure the data is written to memory before DMA reads it
        XCACHE_CleanCacheByRange((uint32_t)completed_buf, BUFFER_SIZE);
    }

    //move to next transfer idx
    g_audio_tx_cur_xfer_idx = (g_audio_tx_cur_xfer_idx + 1) % BUFFER_NUMBER_TX;

}


/*!
 * @brief Initialize audio hardware and create the audio task.
 */
void audio_init(void)
{

	s_AudioTaskQueue = xQueueCreate( 10, sizeof(void*) );
	assert(s_AudioTaskQueue != NULL);

    Audio_InitHardware();

    BaseType_t result = 0;
    result = xTaskCreate(task_audio, "audio", audio_task_STACK_DEPTH, NULL, AUDIO_TASK_PRIORITY, NULL);
    assert(pdPASS == result);


}

static void task_audio(void *param)
{
    sai_transfer_t xfer[BUFFER_NUMBER_RX];
    uint32_t cur_xfer_idx = 0;

    sai_transceiver_t saiConfig;
    PRINTF("SAI task started!\n\r");

    /* Init DMA and create handle for DMA */
#if (!defined(DEMO_EDMA_HAS_CHANNEL_CONFIG) || (defined(DEMO_EDMA_HAS_CHANNEL_CONFIG) && !DEMO_EDMA_HAS_CHANNEL_CONFIG))
    EDMA_GetDefaultConfig(&dmaConfig);
#endif
    EDMA_Init(DEMO_DMA, &dmaConfig);
    EDMA_CreateHandle(&dmaTxHandle, DEMO_DMA, DEMO_TX_EDMA_CHANNEL);
    EDMA_CreateHandle(&dmaRxHandle, DEMO_DMA, DEMO_RX_EDMA_CHANNEL);
#if defined(FSL_FEATURE_EDMA_HAS_CHANNEL_MUX) && FSL_FEATURE_EDMA_HAS_CHANNEL_MUX
    EDMA_SetChannelMux(DEMO_DMA, DEMO_TX_EDMA_CHANNEL, DEMO_SAI_TX_EDMA_CHANNEL);
    EDMA_SetChannelMux(DEMO_DMA, DEMO_RX_EDMA_CHANNEL, DEMO_SAI_RX_EDMA_CHANNEL);
#endif

    /* Set EDMA interrupt priorities */
    NVIC_SetPriority(EDMA0_CH0_IRQn + DEMO_TX_EDMA_CHANNEL, 3);  // TX EDMA priority
    NVIC_SetPriority(EDMA0_CH0_IRQn + DEMO_RX_EDMA_CHANNEL, 2);  // RX EDMA priority (higher priority)

    /* SAI init */
    SAI_Init(DEMO_SAI);

    SAI_TransferTxCreateHandleEDMA(DEMO_SAI, &txHandle, tx_callback, NULL, &dmaTxHandle);
    SAI_TransferRxCreateHandleEDMA(DEMO_SAI, &rxHandle, rx_callback, &cur_xfer_idx, &dmaRxHandle);

    /* I2S mode configurations */
    SAI_GetClassicI2SConfig(&saiConfig, DEMO_AUDIO_BIT_WIDTH, kSAI_Stereo, 1U << DEMO_SAI_CHANNEL);
    saiConfig.syncMode              = DEMO_SAI_TX_SYNC_MODE;
    saiConfig.bitClock.bclkPolarity = DEMO_SAI_TX_BIT_CLOCK_POLARITY;
    saiConfig.masterSlave           = DEMO_SAI_MASTER_SLAVE;
    SAI_TransferTxSetConfigEDMA(DEMO_SAI, &txHandle, &saiConfig);
    saiConfig.syncMode = DEMO_SAI_RX_SYNC_MODE;
    SAI_TransferRxSetConfigEDMA(DEMO_SAI, &rxHandle, &saiConfig);

    /* set bit clock divider */
    SAI_TxSetBitClockRate(DEMO_SAI, DEMO_AUDIO_MASTER_CLOCK, DEMO_AUDIO_SAMPLE_RATE, DEMO_AUDIO_BIT_WIDTH,
                          DEMO_AUDIO_DATA_CHANNEL);
    SAI_RxSetBitClockRate(DEMO_SAI, DEMO_AUDIO_MASTER_CLOCK, DEMO_AUDIO_SAMPLE_RATE, DEMO_AUDIO_BIT_WIDTH,
                          DEMO_AUDIO_DATA_CHANNEL);

    /* master clock configurations */
    AUDIO_MASTER_CLOCK_CONFIG();

    /* Use default setting to init codec */
    if (CODEC_Init(&codecHandle, &boardCodecConfig) != kStatus_Success)
    {
        assert(false);
    }
    if (CODEC_SetVolume(&codecHandle, kCODEC_PlayChannelHeadphoneLeft | kCODEC_PlayChannelHeadphoneRight,
                        DEMO_CODEC_VOLUME) != kStatus_Success)
    {
        assert(false);
    }

    g_audio_codec_ready = true;

    //init transfer structure
    for(int i = 0;i < BUFFER_NUMBER_RX; i++)
    {
    	xfer[i].data = g_AudioRXBuffer + i*BUFFER_SIZE;
    	xfer[i].dataSize = BUFFER_SIZE;

    }

    msg_t* msg = NULL;
    bool free_msg = true;
    while (1)
    {
        if( xQueueReceive(s_AudioTaskQueue, &msg, portMAX_DELAY ) == pdPASS )
        {
            free_msg = true;
            PRINTF("audio task Received msg: %d \r\n", msg->msgID);
            switch(msg->msgID)
            {
                case AUDIO_TASK_RTP_REGISTER_RX_CB:
                    if (g_audio_rx_cb == NULL && g_audio_rx_cb_para == NULL)
                    {
                        if (msg->cb_payload.max_buffer_size < BUFFER_SIZE)
                        {
                            PRINTF("audio rx buffer too small!\n\r");
                            break;
                        }
                        g_audio_rx_cb = msg->cb_payload.callback;
                        g_audio_rx_cb_para = msg->cb_payload.para;

                        //start audio rx from microphone
                        if (kStatus_Success != SAI_TransferReceiveLoopEDMA(DEMO_SAI, &rxHandle, xfer, BUFFER_NUMBER_RX))
                        {
                            PRINTF("SAI rx error!\n\r");
                        }
                    }
                    break;

                case AUDIO_TASK_RTP_DEREGISTER_RX_CB:
                    if (g_audio_rx_cb != NULL && g_audio_rx_cb_para != NULL)
                    {
                        //stop audio rx from microphone
                        SAI_TransferTerminateReceiveEDMA(DEMO_SAI, &rxHandle);
                        g_audio_rx_cb = NULL;
                        g_audio_rx_cb_para = NULL;
                    }
                    break;

                case AUDIO_TASK_RTP_REGISTER_TX_CB:
                    if (g_audio_tx_cb == NULL)
                    {
                        g_audio_tx_cb = msg->cb_payload.callback;
                        g_audio_tx_cb_para = msg->cb_payload.para;
                        PRINTF("TX completion callback registered\n\r");
                    }

                    break;

                case AUDIO_TASK_RTP_DEREGISTER_TX_CB:
                    if (g_audio_tx_cb != NULL)
                    {
                        SAI_TransferTerminateSendEDMA(DEMO_SAI, &txHandle);
                        g_audio_tx_cb = NULL;
                        g_audio_tx_cb_para = NULL;
                        PRINTF("TX completion callback deregistered\n\r");
                    }
                    break;

                default:
                    PRINTF("audio task unknown msg: %d \r\n", msg->msgID);
            }

            if (free_msg && msg != NULL) {
                vPortFree(msg);
            }
        }
    } // end of while (1)
}


void audio_task_notify(uint8_t cmd, msg_t* para)
{
	if (s_AudioTaskQueue == NULL)
	{
		PRINTF("Error: Audio task queue not initialized\r\n");
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
		xQueueSend( s_AudioTaskQueue, &para, portMAX_DELAY );
	}
	else
	{
		BaseType_t xHigherPriorityTaskWoken = pdFALSE;
		xQueueSendFromISR( s_AudioTaskQueue, &para, &xHigherPriorityTaskWoken );
		if (xHigherPriorityTaskWoken)
		{
			portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
		}
	}
}


void audio_start_play(void)
{
    sai_transfer_t xfer[BUFFER_NUMBER_TX];
    status_t status;
    //init transfer structure
    for(int i = 0;i < BUFFER_NUMBER_TX; i++)
    {
    	xfer[i].data = g_AudioTXBuffer + i*BUFFER_SIZE;
    	xfer[i].dataSize = BUFFER_SIZE;

    }

    //start from all "0" data
    memset(g_AudioTXBuffer,0,sizeof(g_AudioTXBuffer));

    if (!g_audio_codec_ready)
    {
        PRINTF("codec not ready!\r\n");
        return;
    }

    // which transfer is on going
    g_audio_tx_cur_xfer_idx = 0;

    if ((status = SAI_TransferSendLoopEDMA(DEMO_SAI, &txHandle, xfer, BUFFER_NUMBER_TX)) != kStatus_Success)
    {
         PRINTF("Error: SAI_TransferSendLoopEDMA:%d\r\n",status);
    }

}
