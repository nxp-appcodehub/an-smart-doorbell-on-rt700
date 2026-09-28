/*
 * Copyright 2019 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "usb_host_config.h"
#include "usb_host.h"
#include "fsl_device_registers.h"
#include "fsl_ostimer.h"
#include "usb_host_video.h"
#include "fsl_common.h"
#include "board.h"
#if (defined(FSL_FEATURE_SOC_SYSMPU_COUNT) && (FSL_FEATURE_SOC_SYSMPU_COUNT > 0U))
#include "fsl_sysmpu.h"
#endif /* FSL_FEATURE_SOC_SYSMPU_COUNT */
#include "app.h"
#include "host_video.h"

#if ((!USB_HOST_CONFIG_KHCI) && (!USB_HOST_CONFIG_EHCI) && (!USB_HOST_CONFIG_OHCI) && (!USB_HOST_CONFIG_IP3516HS))
#error Please enable USB_HOST_CONFIG_KHCI, USB_HOST_CONFIG_EHCI, USB_HOST_CONFIG_OHCI, or USB_HOST_CONFIG_IP3516HS in file usb_host_config.
#endif


/*******************************************************************************
 * Definitions
 ******************************************************************************/
/*******************************************************************************
 * Variables
 ******************************************************************************/
usb_host_handle g_HostHandle;
/*******************************************************************************
 * Prototypes
 ******************************************************************************/
/*!
 * @brief host callback function.
 *
 * device attach/detach callback function.
 *
 * @param deviceHandle          device handle.
 * @param configurationHandle   attached device's configuration descriptor information.
 * @param eventCode             callback event code, please reference to enumeration host_event_t.
 *
 * @retval kStatus_USB_Success              The host is initialized successfully.
 * @retval kStatus_USB_NotSupported         The application don't support the configuration.
 */
static usb_status_t USB_HostEvent(usb_device_handle deviceHandle,
                                  usb_host_configuration_handle configurationHandle,
                                  uint32_t eventCode);

/*!
 * @brief application initialization.
 */
static void USB_HostApplicationInit(void);

/*!
 * @brief host freertos task function.
 *
 * @param g_HostHandle   host handle
 */
static void USB_HostTask(void *param);

/*!
 * @brief host video freertos task function.
 *
 * @param param   the host video instance pointer.
 */
static void USB_HostApplicationTask(void *param);

extern void USB_HostClockInit(void);
extern void USB_HostIsrEnable(void);
extern void USB_HostTaskFn(void *param);
// void BOARD_InitHardware(void);
/*******************************************************************************
 * Code
 ******************************************************************************/

/*!
 * @brief host callback function.
 *
 * device attach/detach callback function.
 *
 * @param deviceHandle           device handle.
 * @param configurationHandle attached device's configuration descriptor information.
 * @param eventCode           callback event code, please reference to enumeration host_event_t.
 *
 * @retval kStatus_USB_Success              The host is initialized successfully.
 * @retval kStatus_USB_NotSupported         The application don't support the configuration.
 */
static usb_status_t USB_HostEvent(usb_device_handle deviceHandle,
                                  usb_host_configuration_handle configurationHandle,
                                  uint32_t eventCode)
{
    usb_status_t status = kStatus_USB_Success;

    switch (eventCode & 0x0000FFFFU)
    {
        case kUSB_HostEventAttach:
            status = USB_HostVideoEvent(deviceHandle, configurationHandle, eventCode);
            break;

        case kUSB_HostEventNotSupported:
            usb_echo("device not supported.\r\n");
            break;

        case kUSB_HostEventEnumerationDone:
            status = USB_HostVideoEvent(deviceHandle, configurationHandle, eventCode);
            break;

        case kUSB_HostEventDetach:
            status = USB_HostVideoEvent(deviceHandle, configurationHandle, eventCode);
            break;

        case kUSB_HostEventEnumerationFail:
            usb_echo("enumeration failed\r\n");
            break;

        default:
            break;
    }
    return status;
}

static void USB_HostApplicationInit(void)
{
    usb_status_t status = kStatus_USB_Success;

    USB_HostClockInit();

#if ((defined FSL_FEATURE_SOC_SYSMPU_COUNT) && (FSL_FEATURE_SOC_SYSMPU_COUNT))
    SYSMPU_Enable(SYSMPU, 0);
#endif /* FSL_FEATURE_SOC_SYSMPU_COUNT */

    status = USB_HostInit(CONTROLLER_ID, &g_HostHandle, USB_HostEvent);
    if (status != kStatus_USB_Success)
    {
        usb_echo("host init error\r\n");
        return;
    }
    USB_HostIsrEnable();

    usb_echo("host init done\r\n");
}

static void USB_HostTask(void *param)
{
    while (1)
    {
        USB_HostTaskFn(param);
    }
}


static void USB_HostApplicationTask(void *param)
{

    while (1)
    {
        USB_HostVideoTask(param);
    }
}

int usb_app_init(void)
{

    USB_HostApplicationInit();
    if (xTaskCreate(USB_HostTask, "usb host task", 1024L / sizeof(portSTACK_TYPE), g_HostHandle, USB_HOST_DRV_TASK_PRI, NULL) != pdPASS)
    {
        usb_echo("create host task error\r\n");
    }

    for (int i = 0;i<USB_HOST_VIDEO_INST_NUM;i++)
    {
		if (xTaskCreate(USB_HostApplicationTask, "CameraTask", 1024L / sizeof(portSTACK_TYPE), &g_Videos[i], USB_HOST_VIDEO_TASK_PRI, NULL) != pdPASS)
		{
			usb_echo("create camera task[%d] error\r\n",i);
		}

    }

    return 0;
}

/* Used for timing statistics. */
void TIMER_Init(void)
{
    /* Intialize the OS timer, setting clock configuration. */
    OSTIMER_Init(OSTIMER_CPU1);
}

configRUN_TIME_COUNTER_TYPE TIMER_GetTimeInUS()
{
	uint64_t us = OSTIMER_GetCurrentTimerValue(OSTIMER_CPU1);
    return us;
}

void vApplicationIdleHook()
{
	   static uint64_t lastIdleCount = 0;
	   static uint64_t lastTotalCount = 0;

	   /* Used to calculate report period. */
	   static uint32_t lastTickCount = 0;
#define IDLE_PERCENT_REPORT_PERIOD  (30*1000) //milliseconds


       if (pdTICKS_TO_MS(xTaskGetTickCount() - lastTickCount) >= IDLE_PERCENT_REPORT_PERIOD)
       {
           uint64_t curCounter = portGET_RUN_TIME_COUNTER_VALUE();
           uint64_t idleCounter = xTaskGetIdleRunTimeCounter();

           //there is a bug in FreeRTOS, when only idle task is ready/running, no task switch
           //task run time counter will not be updated
           //for idle task, the counter is always same with the last time
           //Idle percent should be 100% instead of 0%

           if (idleCounter == lastIdleCount)
           {
        	   usb_echo("Idle percent(5S/Total): %u%%/%u%% \r\n", 100, 100);
           }else
           {
			   usb_echo("Idle percent(5S/Total): %u%%/%u%% \r\n",
					   (uint32_t)((idleCounter - lastIdleCount)* 100 /(curCounter - lastTotalCount)),
					   (uint32_t)ulTaskGetIdleRunTimePercent());
           }


		   //update the global vars
    	   lastIdleCount = idleCounter;
    	   lastTotalCount = curCounter;
    	   lastTickCount = xTaskGetTickCount();
       }

}