/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _APP_MSG_H_
#define _APP_MSG_H_

#include <stdint.h>

#define NAME_SIZE       32

enum {
	IDLE_TASK_PRIORITY_RESERVED = 1,
	SHELL_TASK_PRIORITY_RESERVED = 2,

	UWB_OCPD_TASK_PRIORITY_RESERVED = 4 ,
	UWB_TASK_PRIORITY_RESERVED = 5,

	RTP_TASK_PRIORITY ,
	RTSP_TASK_PRIORITY ,
	MQTT_TASK_PRIORITY = RTSP_TASK_PRIORITY,
	WIFI_TASK_PRIORITY ,

	MPP_TASK_PRIORITY_LOW ,
	MPP_TASK_PRIORITY_HIGH = MPP_TASK_PRIORITY_LOW + 5 ,

    AUDIO_TASK_PRIORITY ,

	CTRL_TASK_PRIORITY ,
	MULTICORE_TASK_PRIORITY,

	TASK_PRIORITY_MAX = configMAX_PRIORITIES - 1, /* max -1 is reserved for timer task. */
};

/* Priority 2 is for Shell and 1 is for Idle task */

enum{

	/* Msg used by control task on core0 */
	CTRL_TASK_WIFI_INIT_DONE,
	CTRL_TASK_WIFI_CONNECTED, //1
	CTRL_TASK_WIFI_CONNECT_FAILED,

	/* Simulate face recognized/unrecoginized event from MPP by Shell cmd */
	CTRL_TASK_SIMU_FACE_NOT_RECOGNIZED,
	CTRL_TASK_SIMU_FACE_RECOGNIZED,

	/* Presence detection or tracking events from UWB task to control task */
	CTRL_TASK_UWB_PRESENCE_DETECTED, //5


	/* multi core task. */
	CTRL_TASK_REMOTE_EP_READY, //6

	/* MPP task msg */
	MPP_TASK_CONFIG,
	MPP_TASK_FR_ENABLE_LCD_ON,
	MPP_TASK_FR_ENABLE_LCD_OFF,  //reserved for future face recognition LCD control
	MPP_TASK_FR_DISABLE_LCD_OFF,
	MPP_TASK_FR_DISABLE_LCD_ON,  //11
	MPP_TASK_FACE_REG_REQ,  //userName and timeout, 12

	CTRL_TASK_FACE_DETECTED,
	CTRL_TASK_FACE_RECOGNIZED, //user name
	CTRL_TASK_FACE_NOT_RECOGNIZED,
	CTRL_TASK_FACE_REG_COMPLETE,
	CTRL_TASK_FACE_REG_TIMEOUT,   //17


	CTRL_TASK_INTERNAL_TIMEOUT, /* Add internal timeout handling for ctrl task*/
	CTRL_TASK_FACE_REG_REQ,  /* Face registration request from shell console to control task */

	CTRL_TASK_MQTT_AV_START,  /* Start audio/video streaming via MQTT */
	CTRL_TASK_MQTT_AV_STOP,   /* Stop audio/video streaming via MQTT */
	CTRL_TASK_MQTT_AV_REJECT,  /* Reject audio/video streaming request */

	CTRL_TASK_SW_PRESSED,  /* SW button press event */


    AUDIO_TASK_RTP_REGISTER_RX_CB,  // Renamed from AUDIO_TASK_RTP_REGISTER_PDU_CB
    AUDIO_TASK_RTP_DEREGISTER_RX_CB, // Renamed from AUDIO_TASK_RTP_DEREGISTER_PDU_CB
    AUDIO_TASK_RTP_REGISTER_TX_CB,   // Renamed from AUDIO_TASK_RTP_REGISTER_TX_COMPLETE_CB
    AUDIO_TASK_RTP_DEREGISTER_TX_CB, // Renamed from AUDIO_TASK_RTP_DEREGISTER_TX_COMPLETE_CB


	USB_HOST_VIDEO_TASK_CMD_LAST = 0xFF
};

typedef struct{
	uint16_t distance[4];
}presence_det_msg_payload_t;


typedef struct{
	char name[NAME_SIZE];
	uint32_t similarity;
}face_recognized_msg_payload_t;


typedef struct{
	char name[NAME_SIZE];
	uint32_t timeout;  //unit: seconds
}face_reg_req_msg_payload_t;

// Unified callback type for both RX and TX
typedef void (*audio_data_cb_t)(uint8_t* buf, uint32_t len, void* para);

// Unified callback registration payload
typedef struct{
    audio_data_cb_t callback;
    void* para;
    uint32_t max_buffer_size;  // For RX: max PDU size; For TX: not used (set to 0)
}audio_rtp_register_cb_payload_t;


typedef struct{
	uint32_t msgID;
    union
	{
		/* data */
		presence_det_msg_payload_t pd_payload;
		face_recognized_msg_payload_t fr_payload;
		face_reg_req_msg_payload_t reg_payload;
		uint32_t para;
        audio_rtp_register_cb_payload_t  cb_payload;
	};

} msg_t;

#endif /* _APP_MSG_H_ */