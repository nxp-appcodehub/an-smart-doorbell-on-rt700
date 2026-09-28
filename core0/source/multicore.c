/*
 * Copyright (c) 2015, Freescale Semiconductor, Inc.
 * Copyright 2016-2025 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fsl_debug_console.h"
#include "board.h"
#include "app_msg.h"
#include "app.h"
#include "hal_camera_shared.h"
#include "rpmsg_multicore.h"
#include "fsl_cache.h"
#include "mpp_task.h"

#include "rpmsg_lite.h"
#include "rpmsg_queue.h"
#include "mcmgr.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/
//#define MCMGR_USED
#define FRAME_BUFFER_LEN (96*1024)
#ifdef MCMGR_USED
#if defined(__ICCARM__) /* IAR Workbench */
#pragma location = "rpmsg_sh_mem_section"
static char rpmsg_lite_base[SH_MEM_TOTAL_SIZE];
#elif defined(__CC_ARM) || defined(__ARMCC_VERSION) /* Keil MDK */
static char rpmsg_lite_base[SH_MEM_TOTAL_SIZE] __attribute__((section("rpmsg_sh_mem_section")));
#elif defined(__GNUC__)
extern uint8_t __RPMSG_SH_MEM_START__[];
extern uint8_t __RPMSG_SH_MEM_END__[];
#define SH_MEM_TOTAL_SIZE ((uint32_t)(__RPMSG_SH_MEM_END__ - __RPMSG_SH_MEM_START__))
// static char rpmsg_lite_base[0] __attribute__((section(".noinit.$rpmsg_sh_mem")));
/*static char s_frame_buf_base[USB_HOST_VIDEO_INST_NUM][FRAME_BUFFER_LEN]
					__attribute__((section(".noinit.$prv_sh_mem"), aligned(CAMERA_DEV_BUFFER_ALIGN)));*/
#else
#error "RPMsg: Please provide your definition of rpmsg_lite_base[]!"
#endif
#endif /* MCMGR_USED */

extern void ctrl_task_notify(uint8_t cmd, void* para);
struct rpmsg_lite_instance *my_rpmsg = NULL;

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

/*******************************************************************************
 * Code
 ******************************************************************************/

#ifdef MCMGR_USED
volatile uint16_t RPMsgRemoteReadyEventData = 0U;
static void RPMsgRemoteReadyEventHandler(mcmgr_core_t coreNum, uint16_t eventData, void *context)
{
    uint16_t *data = (uint16_t *)context;

    *data = eventData;
}

#endif /* MCMGR_USED */

void *getRPMSG_structdata(void)
{
    return (void *)my_rpmsg;
}

volatile uint16_t *get_RPMsgRemoteRdyEventData(void)
{
    return &RPMsgRemoteReadyEventData;
}

void virtCamSendReq(struct rpmsg_lite_endpoint* ep, virtual_usb_cam_msg_type_e msgType, void** buf, uint32_t *len)
{
	virtual_usb_cam_msg_t msg = {
		.msg_type = msgType
	};

	if (msgType == VIRT_USB_CAM_REQRGB)
	{
		msg.msg_payload.req.rgb_frame_addr = (uint32_t)buf[0];
		msg.msg_payload.req.rgb_max_frame_size = len[0];

	}
	else if (msgType == VIRT_USB_CAM_REQIR)
	{
		msg.msg_payload.req.ir_frame_addr = (uint32_t)buf[0];
		msg.msg_payload.req.ir_max_frame_size = len[0];

	}else if(msgType == VIRT_USB_CAM_REQRGBIR)
	{
		msg.msg_payload.req.rgb_frame_addr = (uint32_t)buf[0];
		msg.msg_payload.req.ir_frame_addr = (uint32_t)buf[1];
		msg.msg_payload.req.rgb_max_frame_size = len[0];
		msg.msg_payload.req.ir_max_frame_size = len[1];

	}else{
		assert(0);
	}

	rpmsg_lite_send(my_rpmsg, ep, CORE1_EPT_ADDRESS, (char *)&msg, sizeof(msg), RL_BLOCK);
}



/*!
 * @brief core1 communication task function
 */
static void core1_task(void *para)
{
    bool error_occurred                  = false;

    /* Initialize MCMGR, install generic event handlers */
    (void)MCMGR_Init();

#ifdef CORE1_IMAGE_COPY_TO_RAM
    /* This section ensures the secondary core image is copied from flash location to the target RAM memory.
       It consists of several steps: image size calculation and image copying.
       These steps are not required on MCUXpresso IDE which copies the secondary core image to the target memory during
       startup automatically. */
    uint32_t core1_image_size;
    core1_image_size = get_core1_image_size();
    (void)PRINTF("Copy Secondary core image to address: 0x%x, size: %d\r\n", (void *)(char *)CORE1_BOOT_ADDRESS,
                 core1_image_size);

    /* Copy Secondary core application from FLASH to the target memory. */
    (void)memcpy((void *)(char *)CORE1_BOOT_ADDRESS, (void *)CORE1_IMAGE_START, core1_image_size);

#ifdef APP_INVALIDATE_CACHE_FOR_SECONDARY_CORE_IMAGE_MEMORY
    invalidate_cache_for_core1_image_memory(CORE1_BOOT_ADDRESS, core1_image_size);
#endif /* APP_INVALIDATE_CACHE_FOR_SECONDARY_CORE_IMAGE_MEMORY*/
#endif /* CORE1_IMAGE_COPY_TO_RAM */

    /* Register the application event before starting the secondary core */
    (void)MCMGR_RegisterEvent(kMCMGR_RemoteApplicationEvent,
							RPMsgRemoteReadyEventHandler,
							(void *)&RPMsgRemoteReadyEventData);

    /* Boot Secondary core application */
    (void)PRINTF("Starting core1.\r\n");
    (void)MCMGR_StartCore(kMCMGR_Core1, (void *)(char *)CORE1_BOOT_ADDRESS,
						(uint32_t)__RPMSG_SH_MEM_START__, kMCMGR_Start_Synchronous);

    /* Wait until the secondary core application signals that it has been started. */
    while (APP_RPMSG_READY_EVENT_DATA != RPMsgRemoteReadyEventData)
    {
		vTaskDelay(1);
    };
    (void)PRINTF("Core1 started.\r\n\r\n");


	(void)PRINTF("base:%x  size:%x  id:%d\r\n",__RPMSG_SH_MEM_START__,SH_MEM_TOTAL_SIZE,RPMSG_LITE_LINK_ID);
	#ifdef MCMGR_USED
    my_rpmsg = rpmsg_lite_master_init((void*)__RPMSG_SH_MEM_START__,
                                   SH_MEM_TOTAL_SIZE,
                                   RPMSG_LITE_LINK_ID,
                                   RL_NO_FLAGS);
	#else
	my_rpmsg = rpmsg_lite_master_init((void *)__RPMSG_SH_MEM_START__,
										SH_MEM_TOTAL_SIZE,
										RPMSG_LITE_LINK_ID,
										RL_NO_FLAGS);
	#endif


    if (my_rpmsg == NULL)
    {
        (void)PRINTF("Failed to initialize rpmsg\r\n");
        error_occurred = true;
        goto cleanup;
    }

    /* Wait until the secondary core endpoint is ready. */
    while (APP_EP_READY_EVENT_DATA != RPMsgRemoteReadyEventData)
    {
		vTaskDelay(1);
    };
	PRINTF("Endpoint of remote side is ready\r\n");

	/* Send message to control task to start MPP. */
	ctrl_task_notify(CTRL_TASK_REMOTE_EP_READY,NULL);

	vTaskDelete(NULL);

	return;


cleanup:
    if (my_rpmsg != NULL)
    {
        (void)rpmsg_lite_deinit(my_rpmsg);
        my_rpmsg = NULL;
    }

    /* Print the ending banner */
    if (error_occurred)
    {
        (void)PRINTF("\r\nMulticore task ends with Errors\r\n");
    }
    else
    {
        (void)PRINTF("\r\nMulticore task  ends\r\n");
    }

	while (1)
	{
		vTaskDelay(1000);
	}

}



void core1_init()
{

    /*Camera index as the input para.*/
	if (xTaskCreate(core1_task, "core1_task", 1024,
			(void*)0, MULTICORE_TASK_PRIORITY, NULL) != pdPASS)
	{
		PRINTF("create core1 task error\r\n");
		while (1)
			;
	}
}