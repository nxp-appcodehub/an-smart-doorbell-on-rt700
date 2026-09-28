/*
 * Copyright 2020-2022,2026 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fsl_debug_console.h"

#include "mpp_task.h"
#include "app_msg.h"
#include "fsl_ostimer.h"
#include "uwb_app.h"
#include "rtsp_rtp_server.h"
#include "app.h"
#include "mpp_app_task_notify.h"
#include "mcmgr.h"


#define CTRL_TASK_ENABLE_WIFI 1

enum{
	CTRL_TASK_STATE_INIT = 0,
	CTRL_TASK_STATE_SLEEP,
	CTRL_TASK_STATE_FACE_DETECTION,
    CTRL_TASK_STATE_FACE_RECOGNITION,
    CTRL_TASK_STATE_STANDBY,
    CTRL_TASK_STATE_AV_STREAMING,
	CTRL_TASK_STATE_FACE_REGISTRATION,
	CTRL_TASK_STATE_MAX
};

const char *ctrl_task_state_str[] = {
	"init",
	"sleep",
	"face_det",
	"face_rec",
	"standby",
	"streaming",
	"face_reg"
};

#define CTRL_TASK_STRANGER_NAME "Unknown"
#define CTRL_TASK_TIMER_FACE_DETECT_PERIOD_MS    (10*1000)  // 10 seconds timeout for face detection
#define CTRL_TASK_TIMER_STANDBY_PERIOD_MS        (5*1000) // 5 seconds standby timeout
#define CTRL_TASK_TIMER_FR_PERIOD_MS             (5*1000)  // 5 seconds face recognition timeout
#define CTRL_TASK_TIMER_AV_STREAM_PERIOD_MS     (10*1000) // 10 seconds AV streaming timeout

#define CTRL_TASK_LCD_GPIO GPIO1
#define CTRL_TASK_LCD_BACKLIGHT_PIN 14

#define CTRL_TASK_LCD_BACKLIGHT(x) do {\
	if (x) \
	{\
		GPIO_PortSet(CTRL_TASK_LCD_GPIO, 1UL<<CTRL_TASK_LCD_BACKLIGHT_PIN);\
	}else{\
		GPIO_PortClear(CTRL_TASK_LCD_GPIO, 1UL<<CTRL_TASK_LCD_BACKLIGHT_PIN);\
	}\
}while(0)

enum{
	CTRL_TASK_TIMERID_FACE_DETECT = 0,
	CTRL_TASK_TIMERID_STANDBY,
	CTRL_TASK_TIMERID_FACE_RECOGNITION,
	CTRL_TASK_TIMERID_AV_STREAMING,
	CTRL_TASK_TIMERID_MAX
};


extern void audio_init(void);
extern void demo_wifi_init(void);
extern void core1_init();
extern void demo_mqtt();
static void ctrl_task(void* para);
extern void PublishRingMsg();
extern void *getRPMSG_structdata(void);
extern volatile uint16_t *get_RPMsgRemoteRdyEventData(void);
extern bool isMQTTConEstablished(void);

void ctrl_task_notify(uint8_t cmd, msg_t* para);
static void ctrl_task_internal_timer_handler(uint32_t id);
static void ctrl_task_presence_det_handler(msg_t* msg, TimerHandle_t faceDetTimer);
static void ctrl_task_SW_init(void);
static void change_serial_manager_priority(void);

static QueueHandle_t s_CtrlTaskQueue = NULL;
static uint32_t s_ctrl_task_state = CTRL_TASK_STATE_INIT;
static TimerHandle_t faceDetTimer = NULL;
static TimerHandle_t standbyTimer = NULL;
static TimerHandle_t faceRecTimer = NULL;
static TimerHandle_t AVStartTimer = NULL;


const char* get_ctrl_task_state_str(void)
{
	if (s_ctrl_task_state < CTRL_TASK_STATE_MAX)
	{
		return ctrl_task_state_str[s_ctrl_task_state];
	}
	return "UNKNOWN";
}

static void ctrl_task_enter_state(uint8_t new_state)
{
	s_ctrl_task_state = new_state;
	PRINTF(">> %s\r\n", get_ctrl_task_state_str());

	switch (new_state)
	{
		case CTRL_TASK_STATE_INIT:
			break;
		case CTRL_TASK_STATE_SLEEP:
			mpp_task_notify(MPP_TASK_FR_DISABLE_LCD_OFF,NULL);
			CTRL_TASK_LCD_BACKLIGHT(0);
			break;
		case CTRL_TASK_STATE_FACE_DETECTION:
			mpp_task_notify(MPP_TASK_FR_ENABLE_LCD_ON,NULL);
			CTRL_TASK_LCD_BACKLIGHT(1);

			xTimerReset(faceDetTimer, 0);
			break;
		case CTRL_TASK_STATE_FACE_RECOGNITION:
			xTimerStop(faceDetTimer, 0);
			xTimerReset(faceRecTimer, 0);
			break;
		case CTRL_TASK_STATE_STANDBY:
			mpp_task_notify(MPP_TASK_FR_DISABLE_LCD_ON,NULL);
			xTimerReset(standbyTimer, 0);
			break;
		case CTRL_TASK_STATE_AV_STREAMING:
			mpp_task_notify(MPP_TASK_FR_DISABLE_LCD_ON,NULL);
			xTimerStart(AVStartTimer, 0);
			break;
		case CTRL_TASK_STATE_FACE_REGISTRATION:
			mpp_task_notify(MPP_TASK_FR_DISABLE_LCD_ON,NULL);
			/* stop all timers*/
			xTimerStop(faceDetTimer, 0);
			xTimerStop(faceRecTimer, 0);
			xTimerStop(standbyTimer, 0);
			xTimerStop(AVStartTimer, 0);
			break;
		default:
			break;
	}
}


int main(void)
{
	uint32_t main_clk;

	BOARD_InitHardware();

	main_clk = CLOCK_GetComputeMainClkFreq();
	PRINTF("main clock:%u \r\n",main_clk);

	if (xTaskCreate(ctrl_task, "ctrl_task", 512,
			(void*)NULL, CTRL_TASK_PRIORITY, NULL) != pdPASS)
	{
		PRINTF("create ctrl task error\r\n");
		while (1)
			;
	}

	/* Init SW5(user1) button.*/
	ctrl_task_SW_init();

	core1_init();

	/* Enable wifi.*/
#if CTRL_TASK_ENABLE_WIFI
	demo_wifi_init();
#else
    s_ctrl_task_state = CTRL_TASK_STATE_SLEEP;
#endif

	/* Enable Truesense UWB/RADAR */
	uwb_init();

    audio_init();

	vTaskStartScheduler();

	while (1)
	{
		;
	}
}

/* ISR for switch 5*/
static msg_t s_sw_button_pressed_msg = {0};
void BOARD_SW_IRQ_HANDLER(void)
{
#if (defined(FSL_FEATURE_PORT_HAS_NO_INTERRUPT) && FSL_FEATURE_PORT_HAS_NO_INTERRUPT) || \
    (!defined(FSL_FEATURE_SOC_PORT_COUNT))
    /* Clear external interrupt flag. */
    GPIO_GpioClearInterruptFlags(BOARD_SW_GPIO, 1U << BOARD_SW_GPIO_PIN);
#else
    /* Clear external interrupt flag. */
    GPIO_PortClearInterruptFlags(BOARD_SW_GPIO, 1U << BOARD_SW_GPIO_PIN);
#endif

    /* send a msg to control task. we can't allocate memory here because in ISR */
	ctrl_task_notify(CTRL_TASK_SW_PRESSED,&s_sw_button_pressed_msg);
    SDK_ISR_EXIT_BARRIER;

}


static void ctrl_task_SW_init()
{
	gpio_pin_config_t sw_config = {
        kGPIO_DigitalInput,
        0,
    };

#if (defined(FSL_FEATURE_PORT_HAS_NO_INTERRUPT) && FSL_FEATURE_PORT_HAS_NO_INTERRUPT) || \
    (!defined(FSL_FEATURE_SOC_PORT_COUNT))
    GPIO_SetPinInterruptConfig(BOARD_SW_GPIO, BOARD_SW_GPIO_PIN, kGPIO_InterruptFallingEdge);
#else
    PORT_SetPinInterruptConfig(BOARD_SW_PORT, BOARD_SW_GPIO_PIN, kPORT_InterruptFallingEdge);
#endif
	EnableIRQWithPriority(BOARD_SW_IRQ,BOARD_SW_GPIO_IRQ_PRIORITY);

    GPIO_PinInit(BOARD_SW_GPIO, BOARD_SW_GPIO_PIN, &sw_config);

}

/* Used for timing statistics. */
void TIMER_Init(void)
{
    /* Initialize the OS timer, setting clock configuration. */
    OSTIMER_Init(OSTIMER_CPU0);
}

configRUN_TIME_COUNTER_TYPE TIMER_GetTimeInUS()
{
	uint64_t us = OSTIMER_GetCurrentTimerValue(OSTIMER_CPU0);
    return us;
}

#if CONFIG_COEX_APP
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
        	   PRINTF("Idle percent(5S/Total): %u%%/%u%% \r\n", 100, 100);
           }else
           {
			   PRINTF("Idle percent(5S/Total): %u%%/%u%% \r\n",
					   (uint32_t)((idleCounter - lastIdleCount)* 100 /(curCounter - lastTotalCount)),
					   (uint32_t)ulTaskGetIdleRunTimePercent());
           }


		   //update the global vars
    	   lastIdleCount = idleCounter;
    	   lastTotalCount = curCounter;
    	   lastTickCount = xTaskGetTickCount();
       }

}
#endif


static void ctrl_task_timerCallback( TimerHandle_t pxTimer )
{
	uint32_t id = ( uint32_t ) pvTimerGetTimerID( pxTimer );
	msg_t *msg = (msg_t*)pvPortMalloc(sizeof(msg_t));
	if (msg == NULL)
	{
		PRINTF("ctrl_task_timerCallback: malloc failed, dropping timeout id:%u\r\n", id);
		return;
	}
	msg->para = id;
	ctrl_task_notify(CTRL_TASK_INTERNAL_TIMEOUT,msg);

}


static void ctrl_task(void* para)
{
	msg_t *msg = NULL;
	bool free_msg = true;
	s_CtrlTaskQueue = xQueueCreate( 10, sizeof(void*) );

	assert(s_CtrlTaskQueue != NULL);
	mpp_app_params_t mpp_params = {0};

	/* when there is no face detected for CTRL_TASK_TIMER_FACE_DETECT_PERIOD_MS, go to sleep state */
    faceDetTimer = xTimerCreate("FaceDetTimer",
								pdMS_TO_TICKS(CTRL_TASK_TIMER_FACE_DETECT_PERIOD_MS) , // The timer period in ticks.
	                            pdFALSE,              // One-shot timer (no auto-reload).
	                            (void *)CTRL_TASK_TIMERID_FACE_DETECT,        // timer ID
								ctrl_task_timerCallback       // callback
	                            );

	faceRecTimer = xTimerCreate("FaceRecTimer",
							pdMS_TO_TICKS(CTRL_TASK_TIMER_FR_PERIOD_MS) , // The timer period in ticks.
							pdFALSE,              // One-shot timer (no auto-reload).
							(void *)CTRL_TASK_TIMERID_FACE_RECOGNITION,        // timer ID
							ctrl_task_timerCallback       // callback
							);

	standbyTimer = xTimerCreate("StandbyTimer",
						pdMS_TO_TICKS(CTRL_TASK_TIMER_STANDBY_PERIOD_MS) , // The timer period in ticks.
						pdFALSE,              // One-shot timer (no auto-reload).
						(void *)CTRL_TASK_TIMERID_STANDBY,        // timer ID
						ctrl_task_timerCallback       // callback
						);

	AVStartTimer = xTimerCreate("AVStartTimer",
						pdMS_TO_TICKS(CTRL_TASK_TIMER_AV_STREAM_PERIOD_MS) , // The timer period in ticks.
						pdFALSE,              // One-shot timer (no auto-reload).
						(void *)CTRL_TASK_TIMERID_AV_STREAMING,        // timer ID
						ctrl_task_timerCallback       // callback
						);

    change_serial_manager_priority();


	while(1)
	{

		if( xQueueReceive(s_CtrlTaskQueue, &msg, portMAX_DELAY ) == pdPASS )
		{
			free_msg = true;
			PRINTF("Received msg: %d state:%d\r\n", msg->msgID,s_ctrl_task_state);
			switch(msg->msgID)
			{
				case CTRL_TASK_WIFI_INIT_DONE:
					/* Connect wifi AP , it is done in WIFI task.
					 * if do it here, it will block control task for a long time which
					 * may cause queue overflow.
					 */
					break;
			    case CTRL_TASK_WIFI_CONNECTED:
					/* MQTT is started manually via 'set_broker <ip>' shell command */
					RTSP_Init();
				case CTRL_TASK_WIFI_CONNECT_FAILED:
					/*** after init, enter sleep state ***/
					PRINTF("Init-->Sleep\r\n");
					s_ctrl_task_state = CTRL_TASK_STATE_SLEEP;
					break;

				case CTRL_TASK_SIMU_FACE_NOT_RECOGNIZED:
					/* publish doorbell/ring topic */
					PublishRingMsg("",0);
					break;
				case CTRL_TASK_SIMU_FACE_RECOGNIZED:
					/* publish doorbell/KnowFace */
					PublishRingMsg(msg->fr_payload.name, strlen(msg->fr_payload.name));
					break;

				case CTRL_TASK_UWB_PRESENCE_DETECTED:
					ctrl_task_presence_det_handler(msg, faceDetTimer);
					/* Start MPP if it is inactive.*/
					break;

				case CTRL_TASK_REMOTE_EP_READY:
					mpp_params.mcmgr_event_data_p = get_RPMsgRemoteRdyEventData();
					mpp_params.rpmsg_inst_p = (struct rpmsg_lite_instance *)getRPMSG_structdata();
					mpp_params.recognition_threshold = 0.96f;
					mpp_app_start(&mpp_params);
					break;

				case CTRL_TASK_FACE_DETECTED:
					/* Stop face detection timer and go to face recognition state */
					if (s_ctrl_task_state == CTRL_TASK_STATE_FACE_DETECTION)
					{
						ctrl_task_enter_state(CTRL_TASK_STATE_FACE_RECOGNITION);
					}
					break;
				case CTRL_TASK_FACE_RECOGNIZED: //user name
					PRINTF("Face recognized:%s sim:%d\r\n",msg->fr_payload.name,msg->fr_payload.similarity);
					if (s_ctrl_task_state == CTRL_TASK_STATE_FACE_RECOGNITION)
					{
						xTimerStop(faceRecTimer, 0);
						PublishRingMsg(msg->fr_payload.name, strlen(msg->fr_payload.name));
						ctrl_task_enter_state(CTRL_TASK_STATE_STANDBY);
					}else if (s_ctrl_task_state == CTRL_TASK_STATE_FACE_REGISTRATION) {
						PRINTF("Already registered!\r\n");
						ctrl_task_notify(CTRL_TASK_FACE_REG_COMPLETE, NULL);
					}
					break;

				case CTRL_TASK_FACE_NOT_RECOGNIZED: //user name
					PRINTF("Face not recognized\r\n");
					break;

				case CTRL_TASK_FACE_REG_REQ:
					ctrl_task_enter_state(CTRL_TASK_STATE_FACE_REGISTRATION);
					free_msg = false;
					mpp_task_notify(MPP_TASK_FACE_REG_REQ, msg);
					break;
				case CTRL_TASK_FACE_REG_COMPLETE:
				case CTRL_TASK_FACE_REG_TIMEOUT:
					if (s_ctrl_task_state == CTRL_TASK_STATE_FACE_REGISTRATION)
					{
						ctrl_task_enter_state(CTRL_TASK_STATE_STANDBY);
					}
					break;

				case CTRL_TASK_INTERNAL_TIMEOUT:
					ctrl_task_internal_timer_handler(msg->para);
					break;

				case CTRL_TASK_MQTT_AV_START:
					xTimerStop(AVStartTimer, 0);
					break;
				case CTRL_TASK_MQTT_AV_STOP:
				case CTRL_TASK_MQTT_AV_REJECT:
					if (s_ctrl_task_state == CTRL_TASK_STATE_AV_STREAMING)
					{
						xTimerStop(AVStartTimer, 0);
						ctrl_task_enter_state(CTRL_TASK_STATE_STANDBY);
					}
					break;
				case CTRL_TASK_SW_PRESSED:

					if (s_ctrl_task_state != CTRL_TASK_STATE_INIT
						&& s_ctrl_task_state != CTRL_TASK_STATE_FACE_REGISTRATION
					    && s_ctrl_task_state != CTRL_TASK_STATE_SLEEP)
					{
						/* Create new msg to send to MPP. */
						msg_t* msg_tmp = (msg_t*)pvPortMalloc(sizeof(msg_t));
						if (msg_tmp != NULL)
						{
							memset(msg_tmp->reg_payload.name, 0, sizeof(msg_tmp->reg_payload.name));
							/* to make sure user name will not be duplicated. */
							int sec = TIMER_GetTimeInUS()/1000000UL; // Add timestamp to make unique
							snprintf(msg_tmp->reg_payload.name, sizeof(msg_tmp->reg_payload.name), "user%d", sec);
							msg_tmp->reg_payload.timeout = 10;
							PRINTF("Reg request for %s\r\n", msg_tmp->reg_payload.name);
							ctrl_task_notify(CTRL_TASK_FACE_REG_REQ, msg_tmp);
						}
						else
						{
							PRINTF("SW_PRESSED: malloc failed, dropping reg request\r\n");
						}

					}
					/* Switch event does not need to be released.*/
					free_msg = false;
					break;
				default:
					PRINTF("Unknown message received: %d\r\n", msg->msgID);
					break;
			}

			if (free_msg && msg != NULL) {
				vPortFree(msg);
			}

		}
	}
}

void ctrl_task_notify(uint8_t cmd, msg_t* para)
{
	if (s_CtrlTaskQueue == NULL)
	{
		PRINTF("Error: Control task queue not initialized\r\n");
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
		xQueueSend( s_CtrlTaskQueue, &para, portMAX_DELAY );
	}
	else
	{
		BaseType_t xHigherPriorityTaskWoken = pdFALSE;
		xQueueSendFromISR( s_CtrlTaskQueue, &para, &xHigherPriorityTaskWoken );
		if (xHigherPriorityTaskWoken)
		{
			portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
		}
	}
}



static void ctrl_task_internal_timer_handler(uint32_t id)
{
	PRINTF("Internal timerID:%d\r\n", id);
	switch (id)
	{
		case CTRL_TASK_TIMERID_FACE_DETECT:
			ctrl_task_enter_state(CTRL_TASK_STATE_SLEEP);
			break;
		case CTRL_TASK_TIMERID_FACE_RECOGNITION:
			// issue face not recognized
			if (isMQTTConEstablished())
			{
				PublishRingMsg(CTRL_TASK_STRANGER_NAME, strlen(CTRL_TASK_STRANGER_NAME));
				ctrl_task_enter_state(CTRL_TASK_STATE_AV_STREAMING);
			}else{
				/* If MQTT connection is not established, transition to standby state */
				ctrl_task_enter_state(CTRL_TASK_STATE_STANDBY);
			}
			break;
		case CTRL_TASK_TIMERID_STANDBY:
			ctrl_task_enter_state(CTRL_TASK_STATE_SLEEP);
			break;

		case CTRL_TASK_TIMERID_AV_STREAMING:
			ctrl_task_notify(CTRL_TASK_MQTT_AV_REJECT,NULL);
			break;
		default:
			break;
	}
}

static void ctrl_task_presence_det_handler(msg_t* msg, TimerHandle_t faceDetTimer)
{
	switch(s_ctrl_task_state) {
		case CTRL_TASK_STATE_INIT:
		case CTRL_TASK_STATE_STANDBY:
		case CTRL_TASK_STATE_AV_STREAMING:
		case CTRL_TASK_STATE_FACE_RECOGNITION:
		case CTRL_TASK_STATE_FACE_REGISTRATION:
			/* Ignore presence detection during initialization */

			break;
		case CTRL_TASK_STATE_FACE_DETECTION:
			xTimerReset(faceDetTimer, 0);
			break;

		case CTRL_TASK_STATE_SLEEP:
			/* go to face detection state */
			ctrl_task_enter_state(CTRL_TASK_STATE_FACE_DETECTION);
			break;
		default:
			break;
	}
}


static void change_serial_manager_priority(void)
{
    TaskHandle_t serial_task = xTaskGetHandle("SerialManager_Task");

    if (serial_task != NULL) {
        vTaskPrioritySet(serial_task, 3);
        PRINTF("SerialManager priority changed to %d\r\n", 3);
    }
}