/** @file wifi_task.c
 *
 *  @brief wifi task file
 *
 *  Copyright 2022-2023, 2025 NXP
 *
 *  SPDX-License-Identifier: BSD-3-Clause
 */

///////////////////////////////////////////////////////////////////////////////
//  Includes
///////////////////////////////////////////////////////////////////////////////

#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#include "fsl_debug_console.h"
#include "fsl_shell.h"
#include "wpl.h"
#include "shell_task.h"
#include "app_msg.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define wifi_task_STACK_DEPTH       800
#define DEMO_WIFI_LABEL             "nxp_smart_doorbell"
static char *s_wifi_ssid = "";
static char *s_wifi_password = "";

/*******************************************************************************
 * Variables
 ******************************************************************************/

static volatile bool wlan_connected = false;

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

static shell_status_t cmd_connect(void *shellHandle, int32_t argc, char **argv);
static shell_status_t cmd_scan(void *shellHandle, int32_t argc, char **argv);
static shell_status_t cmd_disconnect(void *shellHandle, int32_t argc, char **argv);

extern void ctrl_task_notify(uint8_t cmd, msg_t* para);

/*******************************************************************************
 * Code
 ******************************************************************************/

SHELL_COMMAND_DEFINE(wlan_scan, "\r\n\"wlan_scan\": Scans networks.\r\n", cmd_scan, 0);

SHELL_COMMAND_DEFINE(wlan_connect_with_password,
                     "\r\n\"wlan_connect_with_password ssid password\":\r\n"
                     "   Connects to the specified network with password.\r\n"
                     " Usage:\r\n"
                     "   ssid:        network SSID\r\n"
                     "   password:    password\r\n",
                     cmd_connect,
                     2);

SHELL_COMMAND_DEFINE(wlan_connect,
                     "\r\n\"wlan_connect ssid\":\r\n"
                     "   Connects to the specified network without password.\r\n"
                     " Usage:\r\n"
                     "   ssid:        network SSID\r\n",
                     cmd_connect,
                     1);

SHELL_COMMAND_DEFINE(wlan_disconnect,
                     "\r\n\"wlan_disconnect\":\r\n"
                     "   Disconnect from connected network\r\n",
                     cmd_disconnect,
                     0);

static void printSeparator(void)
{
    PRINTF("========================================\r\n");
}

/* Link lost callback */
static void LinkStatusChangeCallback(bool linkState)
{
    if (linkState == false)
    {
        PRINTF("-------- LINK LOST --------\r\n");
    }
    else
    {
        PRINTF("-------- LINK REESTABLISHED --------\r\n");
    }
}

void task_wifi(void *param)
{
    wpl_ret_t err = WPLRET_FAIL;
	bool init_suc = false;

    static shell_command_t *wifi_commands[] = {
        SHELL_COMMAND(wlan_scan), SHELL_COMMAND(wlan_connect), SHELL_COMMAND(wlan_connect_with_password),
        SHELL_COMMAND(wlan_disconnect), NULL // end of list
    };

    PRINTF("Initialize WLAN \r\n");
    printSeparator();

    /* Initialize WIFI*/
    err = WPL_Init();
    if (err != WPLRET_SUCCESS)
    {
        PRINTF("[!] WPL_Init: Failed, error: %d\r\n", (uint32_t)err);
		goto start_shell;
    }

    err = WPL_Start(LinkStatusChangeCallback);
    if (err != WPLRET_SUCCESS)
    {
        PRINTF("[!] WPL_Start: Failed, error: %d\r\n", (uint32_t)err);
		goto start_shell;
    }

	init_suc = true;
    PRINTF("Initialize CLI\r\n");

start_shell:
    printSeparator();
    shell_task_init(wifi_commands);

	if (init_suc)
	{
        if (strlen(s_wifi_ssid) != 0)
        {
            /* connect automatically using the pre-configured SSID */
            char* wifi_cmd_line[3] = {
                    "wlan_connect_with_password",
                    s_wifi_ssid,
                    s_wifi_password
            };
            extern shell_handle_t shell_get_handler();
            cmd_connect(shell_get_handler(),
                    sizeof(wifi_cmd_line)/sizeof(wifi_cmd_line[0]),
                    wifi_cmd_line);
        }
        else
        {
            ctrl_task_notify(CTRL_TASK_WIFI_CONNECT_FAILED, NULL);
        }
	}
	else
	{
        /* WPL_Init or WPL_Start failed - notify the control task so the
         * application does not silently wait for a Wi-Fi connected event
         * that will never arrive. */
        ctrl_task_notify(CTRL_TASK_WIFI_CONNECT_FAILED, NULL);
	}

    vTaskDelete(NULL);
}

static shell_status_t cmd_connect(void *shellHandle, int32_t argc, char **argv)
{
    wpl_ret_t err;
    if (wlan_connected == true)
    {
        SHELL_Printf(shellHandle, "Leave network before connecting to a new one!\r\n");
        return kStatus_SHELL_Success;
    }

    /* Remove the network firstly. It may be added wrongly because of WPL_Join bug:
    Even the network does not exist, it still return suc */
    WPL_RemoveNetwork(DEMO_WIFI_LABEL);

    if (argc < 3)
        err = WPL_AddNetwork(argv[1], "", DEMO_WIFI_LABEL);
    else
        err = WPL_AddNetwork(argv[1], argv[2], DEMO_WIFI_LABEL);

    if (err != WPLRET_SUCCESS)
    {
        SHELL_Printf(shellHandle, "Failed to add network profile!\r\n");
        return kStatus_SHELL_Success;
    }

    SHELL_Printf(shellHandle, "Joining: %s\r\n", argv[1]);
    err = WPL_Join(DEMO_WIFI_LABEL);
    if (err != WPLRET_SUCCESS)
    {
        SHELL_Printf(shellHandle, "Failed to join network!\r\n");
        if (WPL_RemoveNetwork(DEMO_WIFI_LABEL) != WPLRET_SUCCESS)
        {
            SHELL_Printf(shellHandle, "Failed to remove network!\r\n");
        }
        wlan_connected = false;
    }
    else{

        SHELL_Printf(shellHandle, "Network joined\r\n");
        char ip[16] = {0};
        err = WPL_GetIP(ip, 1);
        if (err != WPLRET_SUCCESS)
        {
            SHELL_Printf(shellHandle,"Failed to get IP address\r\n");
            wlan_connected = false;
        }else
        {
            SHELL_Printf(shellHandle,"IP address: %s\r\n",ip);
            wlan_connected = true;

        }
    }

    ctrl_task_notify(wlan_connected?CTRL_TASK_WIFI_CONNECTED:CTRL_TASK_WIFI_CONNECT_FAILED, NULL);
    return kStatus_SHELL_Success;
}

static shell_status_t cmd_disconnect(void *shellHandle, int32_t argc, char **argv)
{
    if (wlan_connected == false)
    {
        SHELL_Printf(shellHandle, "No network connected!\r\n");
        return kStatus_SHELL_Success;
    }

    if (WPL_Leave() != WPLRET_SUCCESS)
    {
        SHELL_Printf(shellHandle, "Failed to leave the network!\r\n");
        return kStatus_SHELL_Success;
    }

    if (WPL_RemoveNetwork(DEMO_WIFI_LABEL) != WPLRET_SUCCESS)
    {
        SHELL_Printf(shellHandle, "Failed to remove network profile!\r\n");
        return kStatus_SHELL_Success;
    }

    SHELL_Printf(shellHandle, "Disconnected from network\r\n");
    wlan_connected = false;
    return kStatus_SHELL_Success;
}

static shell_status_t cmd_scan(void *shellHandle, int32_t argc, char **argv)
{
    (void)argc;
    char *scanData = NULL;

    SHELL_Printf(shellHandle, "\r\nInitiating scan...\r\n");
    scanData = WPL_Scan();
    if (scanData == NULL)
    {
        SHELL_Printf(shellHandle, "Error while scanning!\r\n");
    }
    else
    {
        vPortFree(scanData);
    }

#if (defined(SDK_DEBUGCONSOLE) && (SDK_DEBUGCONSOLE == DEBUGCONSOLE_REDIRECT_TO_SDK))
    /*
     * Scanning prints the found networks to the console.
     * Wait for debug console output to be printed before returning from
     * the command, otherwise shell prompt could be printed in
     * the middle of the output of the network scan.
     */
    (void)DbgConsole_Flush();
#endif

    return kStatus_SHELL_Success;
}

void demo_wifi_init(void)
{
    BaseType_t result = 0;
    result = xTaskCreate(task_wifi, "wifi", wifi_task_STACK_DEPTH, NULL, WIFI_TASK_PRIORITY, NULL);
    assert(pdPASS == result);

}
