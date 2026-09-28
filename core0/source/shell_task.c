/*
 * Copyright 2022-2023, 2025 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "shell_task.h"

#include "lwip/sys.h"
#include "lwip/sockets.h"

#include "fsl_component_serial_manager.h"
#include "rtsp_rtp_server.h"
#include "mpp_app_task_notify.h"

#include "app_msg.h"

extern void ctrl_task_notify(uint8_t cmd, msg_t* para);
extern void demo_mqtt(void);
extern void mqtt_set_broker_ip(const char *ip_str);
extern bool mqtt_is_broker_valid(void);

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/*******************************************************************************
 * Prototypes
 ******************************************************************************/
static shell_status_t print_ip_cfg(shell_handle_t shellHandle, int32_t argc, char **argv);
static shell_status_t rtsp_server(shell_handle_t shellHandle, int32_t argc, char **argv);
static shell_status_t simu_face_rec_ind(shell_handle_t shellHandle, int32_t argc, char **argv);
static shell_status_t simu_face_reg(shell_handle_t shellHandle, int32_t argc, char **argv);
static shell_status_t simu_mpp_cmd(shell_handle_t shellHandle, int32_t argc, char **argv);
static shell_status_t simu_pres_det(shell_handle_t shellHandle, int32_t argc, char **argv);
static shell_status_t set_uwb_sensitivity(shell_handle_t shellHandle, int32_t argc, char **argv);
static shell_status_t set_broker(shell_handle_t shellHandle, int32_t argc, char **argv);

static void shell_print_ip(shell_handle_t shellHandle);

/*******************************************************************************
 * Variables
 ******************************************************************************/

SDK_ALIGN(static uint8_t s_shellHandleBuffer[SHELL_HANDLE_SIZE], 4);
static shell_handle_t s_shellHandle;
extern serial_handle_t g_serialHandle;
#define SHELL_MODE_DEFAULT   "SHELL>> "

SHELL_COMMAND_DEFINE(rtsp_server,
                     "\r\n\"rtsp_server\": start or stop rtsp server.\r\n"
                     "Usage:\r\n"
                     "rtsp_server 1  (start the server.)\r\n"
					 "rtsp_server 0  (stop the server.)\r\n",
					 rtsp_server,
                     1);

SHELL_COMMAND_DEFINE(print_ip_cfg, "\r\n\"print_ip_cfg\": Prints IP configuration.\r\n", print_ip_cfg, 0);
SHELL_COMMAND_DEFINE(set_uwb_sensitivity,
                     "\r\n\"set_uwb_sensitivity\": Sets UWB occupancy sensitivity (values between 31 - 37).\r\n"
                     "Usage:\r\n"
                     "set_uwb_sensitivity 31  (sets the sensitivity to 3.1)\r\n"
					 "set_uwb_sensitivity 37  (sets the sensitivity to 3.7)\r\n",
					 set_uwb_sensitivity,
                     1);
SHELL_COMMAND_DEFINE(simu_face_reg,
					"\r\n\"simu_face_reg <name> <timeout>\": simulate a face registration.\r\n"
					"<name>: user name; \r\n"
					"<timeout>: registration timeout in seconds.\r\n",
					simu_face_reg, 2);

SHELL_COMMAND_DEFINE(simu_mpp_cmd,
					"\r\n\"simu_mpp_cmd <enable_fr> <turn_on_dis>\": simulate a MPP command.\r\n"
					"<enable_fr>: 1--enable face recognition; 0--disable face recognition.\r\n"
					"<turn_on_dis>: 1--turn on display; 0--turn off display.\r\n",
					simu_mpp_cmd, 2);

SHELL_COMMAND_DEFINE(simu_pres_det,
					"\r\n\"simu_pres_det\": simulate a presence detection event.\r\n",
					simu_pres_det, 0);


SHELL_COMMAND_DEFINE(simu_face_rec_ind,
					"\r\n\"simu_face_rec_ind <name>\": simulate a face recognition event.\r\n"
					"<name>: user name;  if no <name> is provided, simulate a face unrecognized event.\r\n",
					simu_face_rec_ind, 1);

SHELL_COMMAND_DEFINE(set_broker,
                     "\r\n\"set_broker <ip>\": Set MQTT broker IP address and start MQTT tasks.\r\n"
                     "Usage:\r\n"
                     "set_broker 192.168.1.100\r\n",
                     set_broker, 1);

/*******************************************************************************
 * Code
 ******************************************************************************/
static shell_status_t print_ip_cfg(shell_handle_t shellHandle, int32_t argc, char **argv)
{
    (void)shellHandle;
    (void)argc;

	shell_print_ip(shellHandle);

    return kStatus_SHELL_Success;
}



static shell_status_t rtsp_server(shell_handle_t shellHandle, int32_t argc, char **argv)
{
    (void)shellHandle;
    (void)argc;

	shell_print_ip(shellHandle);

    if (atoi(argv[1]))
    {
    	RTSP_Init();
    }
    else
    {
    	RTSP_DeInit();
    }

    return kStatus_SHELL_Success;
}

extern QueueHandle_t shellCmdQueue;
static shell_status_t set_uwb_sensitivity(shell_handle_t shellHandle, int32_t argc, char **argv)
{
    (void)shellHandle;
    (void)argc;

	int new_sensitivity = atoi(argv[1]);

	if ((new_sensitivity >= 31) && (new_sensitivity <= 45))
	{
		SHELL_Printf(s_shellHandle, "\r\nset uwb sensitivity value: %d\r\n", new_sensitivity);
		float f_sensitivity = new_sensitivity*1.0f/10;
		xQueueSend(shellCmdQueue,&f_sensitivity,pdMS_TO_TICKS(10));
	}
	else
	{
		SHELL_Printf(shellHandle, "Error. Sensitivity parameter out of range\r\n");
	}


    return kStatus_SHELL_Success;
}

static shell_status_t simu_face_rec_ind(shell_handle_t shellHandle, int32_t argc, char **argv)
{
	(void)shellHandle;
    if (argc == 2 && strlen(argv[1]) > 0)
    {
		msg_t* msg = NULL;
		if (strlen(argv[1]) >= sizeof(msg->fr_payload.name)) {
			SHELL_Printf(shellHandle, "Error: Name too long\r\n");
			return kStatus_SHELL_Error;
		}

		if (atoi(argv[1]) == 0) {
			ctrl_task_notify(CTRL_TASK_SIMU_FACE_NOT_RECOGNIZED, NULL);
		}else
		{
			msg = (msg_t*)pvPortMalloc(sizeof(msg_t));
			if (msg == NULL) {
				SHELL_Printf(shellHandle, "Error: out of memory\r\n");
				return kStatus_SHELL_Error;
			}
			memset(msg->fr_payload.name, 0, sizeof(msg->fr_payload.name));
			memcpy(msg->fr_payload.name, argv[1], strlen(argv[1]));
			msg->fr_payload.similarity = 90;
			ctrl_task_notify(CTRL_TASK_SIMU_FACE_RECOGNIZED, msg);

		}
    }
    else
    {
		SHELL_Printf(shellHandle, "Error: name length must > 0\r\n");
    }

    return kStatus_SHELL_Success;
}


static shell_status_t simu_face_reg(shell_handle_t shellHandle, int32_t argc, char **argv)
{
	(void)shellHandle;
    if (argc ==3 && strlen(argv[1]) > 0 && atoi(argv[2]) > 0 && atoi(argv[2]) <= 100)
    {
		msg_t* msg = NULL;
		if (strlen(argv[1]) >= sizeof(msg->reg_payload.name)) {
			SHELL_Printf(shellHandle, "Error: Name too long\r\n");
			return kStatus_SHELL_Error;
		}

		msg = (msg_t*)pvPortMalloc(sizeof(msg_t));
		if (msg == NULL) {
			SHELL_Printf(shellHandle, "Error: out of memory\r\n");
			return kStatus_SHELL_Error;
		}
		memset(msg->reg_payload.name, 0, sizeof(msg->reg_payload.name));
		memcpy(msg->reg_payload.name, argv[1], strlen(argv[1]));
		msg->reg_payload.timeout = atoi(argv[2]);

		ctrl_task_notify(CTRL_TASK_FACE_REG_REQ, msg);
    }
    else
    {
    	SHELL_Printf(shellHandle, "Error:  name length need > 0, timeout > 0 and < 100\r\n");
		return kStatus_SHELL_Error;
    }

    return kStatus_SHELL_Success;
}

static shell_status_t simu_mpp_cmd(shell_handle_t shellHandle, int32_t argc, char **argv)
{
	(void)shellHandle;
    if (argc == 3 && atoi(argv[1]) >= 0 && atoi(argv[1]) <= 1 && atoi(argv[2]) >= 0 && atoi(argv[2]) <= 1)
    {
		bool enable_fr = atoi(argv[1]);
		bool turn_on_dis = atoi(argv[2]);
		uint32_t mpp_cmd_type = MPP_TASK_CONFIG;

		if (enable_fr && turn_on_dis) {
			mpp_cmd_type = MPP_TASK_FR_ENABLE_LCD_ON;
		}else if (enable_fr && !turn_on_dis) {
			mpp_cmd_type = MPP_TASK_FR_ENABLE_LCD_OFF;
		}else if (!enable_fr && turn_on_dis) {
			mpp_cmd_type = MPP_TASK_FR_DISABLE_LCD_ON;
		}else {
			mpp_cmd_type = MPP_TASK_FR_DISABLE_LCD_OFF;
		}

    	mpp_task_notify(mpp_cmd_type, NULL);
    }
    else
    {
    	SHELL_Printf(shellHandle, "Error of command \r\n");
		return kStatus_SHELL_Error;
    }

    return kStatus_SHELL_Success;
}

static shell_status_t simu_pres_det(shell_handle_t shellHandle, int32_t argc, char **argv)
{
	msg_t *msg = (msg_t *)pvPortMalloc(sizeof(msg_t));
	if (msg == NULL) {
		SHELL_Printf(shellHandle, "Error: out of memory\r\n");
		return kStatus_SHELL_Error;
	}
	msg->pd_payload.distance[0] = 50;

	msg->pd_payload.distance[1] = 51;
	msg->pd_payload.distance[2] = 52;
	msg->pd_payload.distance[3] = 53;
	ctrl_task_notify(CTRL_TASK_UWB_PRESENCE_DETECTED, msg);
	return kStatus_SHELL_Success;
}

static shell_status_t set_broker(shell_handle_t shellHandle, int32_t argc, char **argv)
{
    if (argc != 2 || strlen(argv[1]) == 0)
    {
        SHELL_Printf(shellHandle, "Error: provide an IP address, e.g. set_broker 192.168.1.100\r\n");
        return kStatus_SHELL_Error;
    }
    mqtt_set_broker_ip(argv[1]);
    SHELL_Printf(shellHandle, "Broker set to: %s. Starting MQTT...\r\n", argv[1]);
    demo_mqtt();
    return kStatus_SHELL_Success;
}


void shell_task_init(shell_command_t **additional_commands)
{
#if !(defined(SHELL_NON_BLOCKING_MODE) && (SHELL_NON_BLOCKING_MODE > 0U))
#error "Blocking shell is not supported, it does not let run IDLE task and the stacks of self-deleted tasks are not deallocated."
#endif

    /* Init SHELL */
    s_shellHandle = &s_shellHandleBuffer[0];

    /* Shell task is created in SHELL_Init if shell is non-blocking */
    SHELL_Init(s_shellHandle, g_serialHandle, SHELL_MODE_DEFAULT);

    SHELL_RegisterCommand(s_shellHandle, SHELL_COMMAND(print_ip_cfg));
    SHELL_RegisterCommand(s_shellHandle, SHELL_COMMAND(rtsp_server));
	SHELL_RegisterCommand(s_shellHandle, SHELL_COMMAND(set_uwb_sensitivity));
    SHELL_RegisterCommand(s_shellHandle, SHELL_COMMAND(simu_face_reg));
	SHELL_RegisterCommand(s_shellHandle, SHELL_COMMAND(simu_face_rec_ind));
	SHELL_RegisterCommand(s_shellHandle, SHELL_COMMAND(simu_mpp_cmd));
	SHELL_RegisterCommand(s_shellHandle, SHELL_COMMAND(simu_pres_det));
	SHELL_RegisterCommand(s_shellHandle, SHELL_COMMAND(set_broker));


    if (additional_commands != NULL)
    {
        for (; *additional_commands; (void)*additional_commands++)
            SHELL_RegisterCommand(s_shellHandle, *additional_commands);
    }
}


shell_handle_t shell_get_handler()
{
	return s_shellHandle;
}


static void shell_print_ip(shell_handle_t shellHandle)
{
	SHELL_Printf(shellHandle, "===============default net interface information================\r\n");
    SHELL_Printf(shellHandle, " Interface name   : %s%d\r\n", netif_default->name, netif_default->num);
    SHELL_Printf(shellHandle, " IPv4 Address     : %s\r\n", ip4addr_ntoa(netif_ip4_addr(netif_default)));
    SHELL_Printf(shellHandle, " IPv4 Subnet mask : %s\r\n", ip4addr_ntoa(netif_ip4_netmask(netif_default)));
    SHELL_Printf(shellHandle, " IPv4 Gateway     : %s\r\n", ip4addr_ntoa(netif_ip4_gw(netif_default)));
    SHELL_Printf(shellHandle, "================================================================\r\n");
}
