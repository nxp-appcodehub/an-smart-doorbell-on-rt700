/*
 * Copyright (c) 2015, Freescale Semiconductor, Inc.
 * Copyright 2016-2020, 2025 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "board.h"
#include "app.h"
#include "rpmsg_multicore.h"
#include "fsl_debug_console.h"
#include "fsl_gpio.h"
#include "fsl_clock.h"
#include "host_video.h"
#include "fsl_common.h"

#include "rpmsg_lite.h"
#include "rpmsg_queue.h"
#include "mcmgr.h"
#include "hal_camera_shared.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define CAM_BIT_MAP_RGB_IR ((1UL<<USB_HOST_VIDEO_INST_CAM_0) | (1UL<<USB_HOST_VIDEO_INST_CAM_1))
typedef struct{
	virtual_usb_cam_msg_t msg;
	uint32_t remote_addr;
	uint8_t cam_idx_bit_map;  // only used for RGBIR request, indicate which camera device
}frame_cb_para_t;

static struct rpmsg_lite_instance *volatile my_rpmsg = NULL;
static struct rpmsg_lite_endpoint *volatile my_ept = NULL;
static volatile rpmsg_queue_handle my_queue        = NULL;

/*${header:start}*/
gpio_pin_config_t led_config = {
    kGPIO_DigitalOutput,
    0,
};
/*${header:end}*/

/*******************************************************************************
 * Definitions
 ******************************************************************************/
/*${macro:start}*/
#define LED_INIT()   GPIO_PinInit(BOARD_LED_RED_GPIO, BOARD_LED_RED_GPIO_PIN, &led_config);
#define LED_TOGGLE() GPIO_PortToggle(BOARD_LED_RED_GPIO, 1u << BOARD_LED_RED_GPIO_PIN);
/*${macro:end}*/

/*******************************************************************************
 * Prototypes
 ******************************************************************************/
static void led_task(void* para);
static void FrameReqCallback(uint8_t cam_idx, const void* buf, uint32_t len, void* para);
static void multicore_task(void* para);

/*******************************************************************************
 * Code
 ******************************************************************************/


int main(void)
{
	uint32_t main_clk;
	BOARD_InitHardware();

	main_clk = CLOCK_GetSenseMainClkFreq();
	PRINTF("main clock:%u \r\n",main_clk);

	/*Create a led task, so we know core1 is alive. */
	if (xTaskCreate(led_task, "LEDTask", 512L / sizeof(portSTACK_TYPE), NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS)
    {
        PRINTF("create LED task error\r\n");
		while(1);
    }

	if (xTaskCreate(multicore_task, "MultiCoreTask", 1024L, NULL, MULTICORE_TASK_PRI, NULL) != pdPASS)
    {
        PRINTF("create multicore task error\r\n");
		while(1);
    }

	usb_app_init();
	vTaskStartScheduler();
	while (1);

}


static void multicore_task(void* para)
{
	virtual_usb_cam_msg_t usb_msg = {0};
	virtual_usb_cam_msg_t *pUsbCam_msg = &usb_msg;

	/* Initialize MCMGR, install generic event handlers */
    (void)MCMGR_Init();

#ifdef MCMGR_USED
    uint32_t startupData;
    mcmgr_status_t status;

    /* Get the startup data */
    do
    {
		vTaskDelay(1);
        status = MCMGR_GetStartupData(kMCMGR_Core0, &startupData);
    } while (status != kStatus_MCMGR_Success);

    my_rpmsg = rpmsg_lite_remote_init((void *)(char *)(platform_patova(startupData)), RPMSG_LITE_LINK_ID, RL_NO_FLAGS);
    if (my_rpmsg == NULL)
    {
        (void)PRINTF("Failed to initialize rpmsg... %x\r\n",startupData);
        return;
    }

	(void)PRINTF("base:%x  size:%x  id:%d\r\n",startupData,0,RPMSG_LITE_LINK_ID);

    /* Signal the other core we are ready by triggering the event and passing the APP_RPMSG_READY_EVENT_DATA */
    (void)MCMGR_TriggerEvent(kMCMGR_Core0, kMCMGR_RemoteApplicationEvent, APP_RPMSG_READY_EVENT_DATA);
#else
    (void)PRINTF("RPMSG Share Base Addr is 0x%x\r\n", __RPMSG_SH_MEM_START__);
    my_rpmsg = rpmsg_lite_remote_init((void *)__RPMSG_SH_MEM_START__, RPMSG_LITE_LINK_ID, RL_NO_FLAGS);
    if (my_rpmsg == NULL)
    {
        (void)PRINTF("Failed to initialize rpmsg...\r\n");
		return;
    }
#endif /* MCMGR_USED */
    rpmsg_lite_wait_for_link_up(my_rpmsg, RL_BLOCK);
    (void)PRINTF("Link is up!\r\n");


    my_queue  = rpmsg_queue_create(my_rpmsg);
    my_ept    = rpmsg_lite_create_ept(my_rpmsg, CORE1_EPT_ADDRESS, rpmsg_queue_rx_cb, my_queue);

	if (my_queue == NULL || my_ept == NULL)
	{
		PRINTF("queue,ept\r\n");
	}

	/* Signal core0, EP is ready. */
	(void)MCMGR_TriggerEvent(kMCMGR_Core0, kMCMGR_RemoteApplicationEvent, APP_EP_READY_EVENT_DATA);

	uint32_t remote_addr = 0U;
	while(1)
	{
	    rpmsg_queue_recv(my_rpmsg,
						my_queue,
						(uint32_t *)&remote_addr,
						(char *)&usb_msg,
						sizeof(virtual_usb_cam_msg_t),
                        ((void *)0),
						RL_BLOCK);

		switch(pUsbCam_msg->msg_type)
		{
			case VIRT_USB_CAM_CONFIG:
				/* setup USB camera */
				pUsbCam_msg->msg_type = VIRT_USB_CAM_CONFIG_ACK;
				(void)rpmsg_lite_send(my_rpmsg, my_ept, remote_addr,
								(char *)pUsbCam_msg, sizeof(virtual_usb_cam_msg_t),
								RL_BLOCK);
				break;
			case VIRT_USB_CAM_REQRGB:
			case VIRT_USB_CAM_REQIR:
			{
				/* save the original message and remote address*/
				frame_cb_para_t* tmp = (frame_cb_para_t*)pvPortMalloc(sizeof(frame_cb_para_t));
				memcpy(&tmp->msg,pUsbCam_msg,sizeof(virtual_usb_cam_msg_t));
				tmp->remote_addr = remote_addr;
				USB_HostVideoSendMsg2Inst(USB_HOST_VIDEO_TASK_REG_FRAME_CB_REQ,
						FrameReqCallback,
						(void*)tmp, /*parameter for the callback*/
						(uint32_t)pUsbCam_msg->msg_type == VIRT_USB_CAM_REQRGB ?
						USB_HOST_VIDEO_INST_CAM_0 : USB_HOST_VIDEO_INST_CAM_1);
				break;
			}

			case VIRT_USB_CAM_REQRGBIR:
				/* assigned IR & RGB buffer */
			{
				// pUsbCam_msg->msg_type = VIRT_USB_CAM_RSPRGBIR;
				frame_cb_para_t* tmp = (frame_cb_para_t*)pvPortMalloc(sizeof(frame_cb_para_t));
				memcpy(&tmp->msg,pUsbCam_msg,sizeof(virtual_usb_cam_msg_t));
				tmp->remote_addr = remote_addr;
				tmp->cam_idx_bit_map = 0;

				/* Split into a RGB and IR request */
				USB_HostVideoSendMsg2Inst(USB_HOST_VIDEO_TASK_REG_FRAME_CB_REQ,
						FrameReqCallback,
						(void*)tmp, /*parameter for the callback*/
						USB_HOST_VIDEO_INST_CAM_0);

				USB_HostVideoSendMsg2Inst(USB_HOST_VIDEO_TASK_REG_FRAME_CB_REQ,
						FrameReqCallback,
						(void*)tmp, /*parameter for the callback*/
						USB_HOST_VIDEO_INST_CAM_1);
				break;
			}
			default:
				pUsbCam_msg->msg_type = VIRT_USB_CAM_CONFIG_ERR;
				break;
		}

	}
}

/*!
 * @brief Led task function
 */
static void led_task(void* para)
{

    /* Configure LED */
    LED_INIT();
    for (;;)
    {
		vTaskDelay(pdMS_TO_TICKS(1000));
        LED_TOGGLE();
    }
}


static void CopyRGBFrame(const void* buf, uint32_t len, virtual_usb_cam_msg_t *msg)
{
		if (msg->msg_payload.req.rgb_max_frame_size < len)
		{
			PRINTF("RGB frame buffer overflow!\r\n");
			msg->msg_payload.rsp.rgb_frame_size = 0;
		}else{
			memcpy((void*)msg->msg_payload.req.rgb_frame_addr,buf,len);
			msg->msg_payload.rsp.rgb_frame_size = len;
			msg->msg_payload.rsp.rgb_frame_addr = msg->msg_payload.req.rgb_frame_addr;

		}

}

static void CopyIRFrame(const void* buf, uint32_t len, virtual_usb_cam_msg_t *msg)
{
	if ( msg->msg_payload.req.ir_max_frame_size < len)
	{
		PRINTF("IR frame buffer overflow!\r\n");
		msg->msg_payload.rsp.ir_frame_size = 0;

	}else
	{
		memcpy((void*)msg->msg_payload.req.ir_frame_addr,buf,len);
		msg->msg_payload.rsp.ir_frame_size = len;
		msg->msg_payload.rsp.ir_frame_addr = msg->msg_payload.req.ir_frame_addr;
	}

}

/* This call back is called in USB host task context.
 * Copy frame data from USB host internal frame buffer to sharing buffer of multicore
 * and notify remote side, data is ready.
 */
static void FrameReqCallback(uint8_t cam_idx, const void* buf, uint32_t len, void* para)
{
	// uint32_t buf_id = (uint32_t)para;
	frame_cb_para_t *cb_para  = (frame_cb_para_t*)para;
	virtual_usb_cam_msg_t *msg = &cb_para->msg;

	if (msg->msg_type == VIRT_USB_CAM_REQRGB)
	{
		CopyRGBFrame(buf,len,msg);
		msg->msg_type = VIRT_USB_CAM_RSPRGB;
		(void)rpmsg_lite_send(my_rpmsg, my_ept, cb_para->remote_addr,
						(char *)msg,
						sizeof(virtual_usb_cam_msg_t),
						RL_BLOCK);

		// release the memory
		vPortFree(para);
	}
	else if (msg->msg_type == VIRT_USB_CAM_REQIR)
	{
		CopyIRFrame(buf,len,msg);

		msg->msg_type = VIRT_USB_CAM_RSPIR;
		(void)rpmsg_lite_send(my_rpmsg, my_ept, cb_para->remote_addr,
						(char *)msg,
						sizeof(virtual_usb_cam_msg_t),
						RL_BLOCK);
		// release the memory
		vPortFree(para);
	}else if (msg->msg_type == VIRT_USB_CAM_REQRGBIR)
	{
		if (cam_idx == USB_HOST_VIDEO_INST_CAM_0)
		{
			CopyRGBFrame(buf,len,msg);
		}else
		{
			CopyIRFrame(buf,len,msg);

		}

		/* As this callback may be called in multi task context.
		 *we need protect this set-test-clear condition.
		 */
		vTaskSuspendAll();
		cb_para->cam_idx_bit_map |= 1UL<<cam_idx;

		if ((cb_para->cam_idx_bit_map & CAM_BIT_MAP_RGB_IR) == CAM_BIT_MAP_RGB_IR)
		{
			/* Clear the bit map in time.*/
			cb_para->cam_idx_bit_map = 0;
			// taskEXIT_CRITICAL();
			xTaskResumeAll();
			//both RGB and IR received
			msg->msg_type = VIRT_USB_CAM_RSPRGBIR;
			(void)rpmsg_lite_send(my_rpmsg, my_ept,
				cb_para->remote_addr,
				(char *)msg,
				sizeof(virtual_usb_cam_msg_t),
				RL_BLOCK);
			// release the memory
			vPortFree(para);
		}else{
			xTaskResumeAll();
		}

	}

}