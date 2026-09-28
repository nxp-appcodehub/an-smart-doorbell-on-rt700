/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _APP_H_
#define _APP_H_

#include "usb_host_config.h"
#include "usb_host.h"
#include "fsl_device_registers.h"
// #include "rpmsg_lite.h"
// #include "rpmsg_queue.h"
// #include "mcmgr.h"
// #include "hal_camera_shared.h"

#if defined(__cplusplus)
extern "C" {
#endif /* __cplusplus */

/*******************************************************************************
 * Prototypes
 ******************************************************************************/
/*${prototype:start}*/
void BOARD_InitHardware(void);
/*${prototype:end}*/



/*******************************************************************************
 * Definitions
 ******************************************************************************/

/* @TEST_ANCHOR */

#if ((defined USB_HOST_CONFIG_KHCI) && (USB_HOST_CONFIG_KHCI))
#ifndef CONTROLLER_ID
#define CONTROLLER_ID kUSB_ControllerKhci0
#endif
#endif /* USB_HOST_CONFIG_KHCI */
#if ((defined USB_HOST_CONFIG_EHCI) && (USB_HOST_CONFIG_EHCI))
#ifndef CONTROLLER_ID
#define CONTROLLER_ID kUSB_ControllerEhci0
#endif
#endif /* USB_HOST_CONFIG_EHCI */
#if ((defined USB_HOST_CONFIG_OHCI) && (USB_HOST_CONFIG_OHCI))
#ifndef CONTROLLER_ID
#define CONTROLLER_ID kUSB_ControllerOhci0
#endif
#endif /* USB_HOST_CONFIG_OHCI */
#if ((defined USB_HOST_CONFIG_IP3516HS) && (USB_HOST_CONFIG_IP3516HS))
#ifndef CONTROLLER_ID
#define CONTROLLER_ID kUSB_ControllerIp3516Hs0
#endif
#endif /* USB_HOST_CONFIG_IP3516HS */

#if defined(__GIC_PRIO_BITS)
#define USB_HOST_INTERRUPT_PRIORITY (25U)
#else
#define USB_HOST_INTERRUPT_PRIORITY (6U)
#endif

/* configMAX_PRIORITIES is 18, 17 reserved for timer task */
#define USB_HOST_DRV_TASK_PRI (configMAX_PRIORITIES - 2)  //16
#define USB_HOST_VIDEO_TASK_PRI (USB_HOST_DRV_TASK_PRI - 1) //15
#define MULTICORE_TASK_PRI (USB_HOST_VIDEO_TASK_PRI - 2) //13


/* To support multiple instances */
enum{
	USB_HOST_VIDEO_INST_CAM_0 = 0,
	USB_HOST_VIDEO_INST_CAM_1,
	USB_HOST_VIDEO_INST_NUM,
	USB_HOST_VIDEO_INST_INVALID = 0xFF,
};


/*! @brief host app device attach/detach status */
typedef enum _usb_host_app_state
{
    kStatus_DEV_Idle = 0, /*!< there is no device attach/detach */
    kStatus_DEV_Attached, /*!< device is attached */
    kStatus_DEV_Detached, /*!< device is detached */
} usb_host_app_state_t;


int usb_app_init(void);

static inline uint32_t getTime()
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

#endif /* __APP_H__ */
