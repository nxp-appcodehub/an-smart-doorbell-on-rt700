/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core_mqtt.h"

#include "transport_interface.h"
#include "lwip/sockets.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include <stdio.h>
#include "wpl.h"
#include "core_mqtt_serializer.h"
#include "lwip/netdb.h"
#include "app_msg.h"


#define mqtt_task_STACK_DEPTH       1024
#define DEMO_MQTT_LABEL             "DoorbellMQTT"

/* Broker IP is set at runtime via the 'set_broker' shell command.
 * Default "0.0.0.0" means no valid broker has been configured yet. */
static char s_broker_address[16] = "0.0.0.0";
#define BROKER_PORT    1883
#define CLIENT_ID      "RT700_smart_doorbell"

#define TOPIC_NAME1     "doorbell/ring"
#define PAYLOAD1        "unknowner"

#define TOPIC_NAME3     "doorbell/heartbeat"
#define PAYLOAD3        "ping"

#define MQTT_PACKET_BUFFER_SIZE          2048U
#define MQTT_KEEP_ALIVE_INTERVAL_SECONDS 60U


/* for QoS 1 and 2, define a counter for message tracking */
static uint16_t usMessageIdentifier = 1;
static uint8_t ucMQTT_Connection_Established = 0;

/* Task handles - used to detect/prevent duplicate tasks and to allow
 * mqtt_task to clean up mqtt_heartbeat_task on fatal exit. */
static TaskHandle_t s_mqttTaskHandle   = NULL;
static TaskHandle_t s_mqttHbTaskHandle = NULL;

enum{
	MQTT_PUBLISH_DOORBELL_RING,
	MQTT_PUBLISH_DOORBELL_HEARTBEAT,
	MQTT_PUBLISH_MAX
};

static MQTTPublishInfo_t s_publishInfos[] = {
	{
		.qos = MQTTQoS0,
		.pTopicName = TOPIC_NAME1,
		.pPayload = PAYLOAD1,
		.retain = false,
		.dup = false
	},

	{
		.qos = MQTTQoS0,
		.pTopicName = TOPIC_NAME3,
		.pPayload = PAYLOAD3,
		.retain = false,
		.dup = false
	}

};

typedef void (*IncomingMessageCallback_t)(MQTTPublishInfo_t *pPublishInfo);
static void doorbellStartCallback(MQTTPublishInfo_t *pPublishInfo);
static void doorbellRejectCallback(MQTTPublishInfo_t *pPublishInfo);
static void doorbellEndCallback(MQTTPublishInfo_t *pPublishInfo);
static int32_t Transport_Send(NetworkContext_t * pNetworkContext, const void * pBuffer, size_t bytesToSend);
static int32_t Transport_Recv(NetworkContext_t * pNetworkContext, void * pBuffer, size_t bytesToRecv);

typedef struct NetworkContext
{
    int socket;
} NetworkContext_t;

typedef struct
{
    const char *topic;
    IncomingMessageCallback_t callback;
} TopicCallbackEntry;


TopicCallbackEntry callbackTable[] = {
    { "doorbell/start", doorbellStartCallback },
    { "doorbell/reject", doorbellRejectCallback },
	{ "doorbell/end", doorbellEndCallback }
    // Add more entries as needed
};
#define NUM_TOPICS_SUBSCRIBE (sizeof(callbackTable) / sizeof(callbackTable[0]))

MQTTContext_t mqttContext = {0};
static NetworkContext_t transportSocket;
static uint8_t buffer[MQTT_PACKET_BUFFER_SIZE];

static TransportInterface_t transportInterface = {
	.send = Transport_Send,
	.recv = Transport_Recv,
	.pNetworkContext = &transportSocket
};

static MQTTFixedBuffer_t networkBuffer = {
	.pBuffer = buffer,
	.size = MQTT_PACKET_BUFFER_SIZE
};

extern void ctrl_task_notify(uint8_t cmd, msg_t* para);

static bool isWifiConnected(void)
{
	char ip[16] = {0};
	wpl_ret_t result = WPL_GetIP(ip, 1);
	if (result != WPLRET_SUCCESS)
	{
		PRINTF("WPL_GetIP failed: %d\r\n", result);
	    return false;
	}
    return true;
}

bool isMQTTConEstablished(void)
{
	return (ucMQTT_Connection_Established == 1);
}


void PublishPingMsg()
{
	if (ucMQTT_Connection_Established == 0)
	{
		PRINTF("MQTT connection not established. Cannot publish heartbeat.\r\n");
		return;
	}
	s_publishInfos[MQTT_PUBLISH_DOORBELL_HEARTBEAT].topicNameLength =
					strlen(s_publishInfos[MQTT_PUBLISH_DOORBELL_HEARTBEAT].pTopicName);
	s_publishInfos[MQTT_PUBLISH_DOORBELL_HEARTBEAT].payloadLength = strlen(s_publishInfos[MQTT_PUBLISH_DOORBELL_HEARTBEAT].pPayload);

	 MQTTStatus_t Status = MQTT_Publish(&mqttContext, &s_publishInfos[MQTT_PUBLISH_DOORBELL_HEARTBEAT],255);
	 if (Status != MQTTSuccess)
	 {
		 PRINTF(" PublishPingMsg Failed %d \r\n", Status);
	 }
}



void PublishRingMsg(char* name, uint16_t nameLen)
{
	if (ucMQTT_Connection_Established == 0)
	{
		PRINTF("MQTT connection not established. Cannot publish ring message.\r\n");
		return;
	}

	s_publishInfos[MQTT_PUBLISH_DOORBELL_RING].topicNameLength =
					strlen(s_publishInfos[MQTT_PUBLISH_DOORBELL_RING].pTopicName);

	s_publishInfos[MQTT_PUBLISH_DOORBELL_RING].pPayload = name;
	s_publishInfos[MQTT_PUBLISH_DOORBELL_RING].payloadLength = nameLen;

	 MQTTStatus_t Status = MQTT_Publish(&mqttContext, &s_publishInfos[MQTT_PUBLISH_DOORBELL_RING],usMessageIdentifier++);
	 if (Status != MQTTSuccess)
	 {
		 PRINTF(" PublishRingMsg Failed %d \r\n", Status);
	 }
}


static void doorbellStartCallback(MQTTPublishInfo_t *pPublishInfo)
{
    PRINTF("doorbell Start Callback received:\r\n");
    PRINTF("Topic: %.*s\n", pPublishInfo->topicNameLength, pPublishInfo->pTopicName);
    PRINTF("Payload: %.*s\n", pPublishInfo->payloadLength, pPublishInfo->pPayload);
	ctrl_task_notify(CTRL_TASK_MQTT_AV_START,NULL);
}
static void doorbellRejectCallback(MQTTPublishInfo_t *pPublishInfo)
{
    PRINTF("doorbell Reject Callback received:\r\n");
    PRINTF("Topic: %.*s\n", pPublishInfo->topicNameLength, pPublishInfo->pTopicName);
    PRINTF("Payload: %.*s\n", pPublishInfo->payloadLength, pPublishInfo->pPayload);
	ctrl_task_notify(CTRL_TASK_MQTT_AV_REJECT,NULL);
}
static void doorbellEndCallback(MQTTPublishInfo_t *pPublishInfo)
{
    PRINTF("doorbell End Callback received:\n");
    PRINTF("Topic: %.*s\n", pPublishInfo->topicNameLength, pPublishInfo->pTopicName);
    PRINTF("Payload: %.*s\n", pPublishInfo->payloadLength, pPublishInfo->pPayload);
	ctrl_task_notify(CTRL_TASK_MQTT_AV_STOP,NULL);
}

static uint32_t MQTT_GetCurrentTime(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static void mqttEventCallback(MQTTContext_t * pMqttContext,
                       MQTTPacketInfo_t * pPacketInfo,
                       MQTTDeserializedInfo_t * pDeserializedInfo)
{
    if ((pPacketInfo->type & 0xF0U) == MQTT_PACKET_TYPE_PUBLISH &&
        pDeserializedInfo->pPublishInfo != NULL)
    {
        const char *incomingTopic = pDeserializedInfo->pPublishInfo->pTopicName;
        uint16_t topicLength = pDeserializedInfo->pPublishInfo->topicNameLength;

        for (int i = 0; i < sizeof(callbackTable) / sizeof(callbackTable[0]); i++)
        {
            const char *registeredTopic = callbackTable[i].topic;
            size_t registeredLength = strlen(registeredTopic);

            if (topicLength == registeredLength &&
                strncmp(incomingTopic, registeredTopic, topicLength) == 0)
            {
                callbackTable[i].callback(pDeserializedInfo->pPublishInfo);
                break;
            }
        }
    }
}

static int32_t Transport_Send(NetworkContext_t * pNetworkContext, const void * pBuffer, size_t bytesToSend)
{
    int sock = pNetworkContext->socket;
    int sent = lwip_send(sock, pBuffer, bytesToSend, 0);
    return (sent >= 0) ? sent : -1;
}

static int32_t Transport_Recv(NetworkContext_t * pNetworkContext, void * pBuffer, size_t bytesToRecv)
{
    int sock = pNetworkContext->socket;
    int received = lwip_recv(sock, pBuffer, bytesToRecv, 0);
    return (received >= 0) ? received : -1;
}

static uint8_t Transport_Connect(NetworkContext_t * pSocket, const char * address, uint16_t port)
{
    struct sockaddr_in brokerAddr;
    struct hostent * host;
    int sock;

    // Resolve broker address
    host = gethostbyname(address);
    if (host == NULL)
    {
    	PRINTF("DNS FAILED\r\n");
        return 1; // DNS resolution failed
    }

    // Create socket
    sock = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0)
    {
    	PRINTF("SOCKET CREATION FAILED\r\n");
        return 2; // Socket creation failed
    }

    // Prepare broker address
    memset(&brokerAddr, 0, sizeof(brokerAddr));
    brokerAddr.sin_family = AF_INET;
    brokerAddr.sin_port = htons(port);
    // memcpy(&brokerAddr.sin_addr, host->h_addr, host->h_length);
	inet_aton(address, &brokerAddr.sin_addr);

	PRINTF("broker:%s\r\n",inet_ntoa(brokerAddr.sin_addr));

    // Connect to broker
    if (lwip_connect(sock, (struct sockaddr *)&brokerAddr, sizeof(brokerAddr)) < 0)
    {
    	PRINTF("SOCKET CONNECT FAILED\r\n");
        lwip_close(sock);
        return 3; // Connection failed
    }

    // Save socket
    pSocket->socket = sock;
    return 0; // Success
}

/* Disconnect the transport (TCP/TLS socket) */
static uint8_t Transport_Disconnect(NetworkContext_t * pSocket)
{
    if (pSocket == NULL)
    {
        return 1; // Invalid pointer
    }

    if (pSocket->socket >= 0)
    {
        // Close the socket
        lwip_close(pSocket->socket);
        PRINTF("[i] Transport disconnected, socket %d closed\r\n", pSocket->socket);

        // Reset socket field
        pSocket->socket = -1;
        return 0; // Success
    }
    else
    {
        PRINTF("[!] Transport disconnect called but socket invalid\r\n");
        return 2; // No valid socket to close
    }
}

static void mqtt_task(void *pvParameters)
{
	uint8_t resultVal;
	MQTTStatus_t mqttStatus = MQTTNoDataAvailable;

reconnect:
	while (!isWifiConnected())
	{
		PRINTF("Waiting for Wi-Fi connection...\r\n");
		vTaskDelay(pdMS_TO_TICKS(5000));
	}

	{
		int connectTries = 0;
		const int maxConnectTries = 3;
		resultVal = 1;
		while (connectTries < maxConnectTries)
		{
			resultVal = Transport_Connect(&transportSocket, s_broker_address, BROKER_PORT);
			if (resultVal == 0)
			{
				break;
			}
			connectTries++;
			PRINTF("Transport connection failed (attempt %d/%d). Retrying in 5s...\r\n",
			       connectTries, maxConnectTries);
			vTaskDelay(pdMS_TO_TICKS(5000));
		}
		if (resultVal != 0)
		{
			PRINTF("[MQTT] Could not connect to broker after %d attempts. Stopping MQTT task.\r\n",
			       maxConnectTries);
			/* Kill the companion heartbeat task if it is still alive, then clean up handles. */
			if (s_mqttHbTaskHandle != NULL)
			{
			    vTaskDelete(s_mqttHbTaskHandle);
			    s_mqttHbTaskHandle = NULL;
			}
			s_mqttTaskHandle = NULL;
			vTaskDelete(NULL);
			return;
		}
	}

	PRINTF("Transport connection is done!\r\n");

	MQTTConnectInfo_t connectInfo = {
	    .pClientIdentifier = CLIENT_ID,
	    .clientIdentifierLength = strlen(CLIENT_ID),
	    .cleanSession = true,
	    .keepAliveSeconds = MQTT_KEEP_ALIVE_INTERVAL_SECONDS,
	    .pUserName = NULL,
	    .userNameLength = 0,
	    .pPassword = NULL,
	    .passwordLength = 0
	};

    MQTTSubscribeInfo_t subscribeInfo[NUM_TOPICS_SUBSCRIBE];


    for (size_t i = 0; i < NUM_TOPICS_SUBSCRIBE; i++) {
        subscribeInfo[i].qos = MQTTQoS0;
        subscribeInfo[i].pTopicFilter = callbackTable[i].topic;
        subscribeInfo[i].topicFilterLength = strlen(callbackTable[i].topic);
    }

    bool sessionPresent = false;
    vTaskDelay(pdMS_TO_TICKS(50));

	mqttStatus = MQTT_Connect(&mqttContext, &connectInfo, NULL, 10000, &sessionPresent);

    if (mqttStatus != MQTTSuccess)
    {
        PRINTF(" MQTT_Connect failed with status: %d\r\n", mqttStatus);
        Transport_Disconnect(&transportSocket);
        if (s_mqttHbTaskHandle != NULL)
        {
            vTaskDelete(s_mqttHbTaskHandle);
            s_mqttHbTaskHandle = NULL;
        }
        s_mqttTaskHandle = NULL;
        vTaskDelete(NULL);
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(50));

    if(!sessionPresent)
    {
    	mqttStatus = MQTT_Subscribe(&mqttContext, subscribeInfo, sizeof(subscribeInfo)/sizeof(subscribeInfo[0]),  1);
    	PRINTF(" MQTT_Subscribe COMPL %d \r\n", mqttStatus);
    	vTaskDelay(pdMS_TO_TICKS(1000));
    }

	ucMQTT_Connection_Established = 1;  // Flag to indicate successful MQTT connection


    for (;;)
    {
    	mqttStatus = MQTT_ProcessLoop(&mqttContext);

    	if (mqttStatus != MQTTSuccess) {
    	    PRINTF("MQTT_ProcessLoop failed with status %d\r\n", mqttStatus);
			Transport_Disconnect(&transportSocket);
			ucMQTT_Connection_Established = 0;
			goto reconnect;
    	}

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void mqtt_heartbeat_task(void *pvParameters)
{
	/* Wait for the MQTT connection to be established or for mqtt_task to exit. */
	while (!ucMQTT_Connection_Established)
	{
		if (s_mqttTaskHandle == NULL)
		{
			/* mqtt_task has exited without ever connecting - stop heartbeat too. */
			PRINTF("[MQTT HB] mqtt_task gone before connection. Stopping heartbeat task.\r\n");
			s_mqttHbTaskHandle = NULL;
			vTaskDelete(NULL);
			return;
		}
		vTaskDelay(pdMS_TO_TICKS(2000));
	}

	for (;;)
	{
		/* If mqtt_task has exited (handle cleared), stop the heartbeat too. */
		if (s_mqttTaskHandle == NULL)
		{
			PRINTF("[MQTT HB] mqtt_task exited. Stopping heartbeat task.\r\n");
			s_mqttHbTaskHandle = NULL;
			vTaskDelete(NULL);
			return;
		}
		//ping msg to broker for every MQTT_KEEP_ALIVE_INTERVAL_SECONDS sec to not disconnect from broker
		PublishPingMsg();
		vTaskDelay(pdMS_TO_TICKS(MQTT_KEEP_ALIVE_INTERVAL_SECONDS * 1000));
	}
}

/* Set the MQTT broker IP at runtime. Call before demo_mqtt(). */
void mqtt_set_broker_ip(const char *ip_str)
{
    strncpy(s_broker_address, ip_str, sizeof(s_broker_address) - 1);
    s_broker_address[sizeof(s_broker_address) - 1] = '\0';
    PRINTF("[MQTT] Broker IP set to: %s\r\n", s_broker_address);
}

/* Returns true when a non-default broker address has been configured. */
bool mqtt_is_broker_valid(void)
{
    return (strcmp(s_broker_address, "0.0.0.0") != 0);
}

void demo_mqtt(void)
{
	if (!mqtt_is_broker_valid())
	{
	    PRINTF("[MQTT] Broker IP not set. Use 'set_broker <ip>' first.\r\n");
	    return;
	}

	/* Prevent duplicate task creation if mqtt_task is already running. */
	if (s_mqttTaskHandle != NULL)
	{
	    PRINTF("[MQTT] MQTT tasks already running. Use 'set_broker' only after they have stopped.\r\n");
	    return;
	}

	BaseType_t result = 0;
	ucMQTT_Connection_Established = 0;

	// Clear context.
	memset( ( void * ) &mqttContext, 0x00, sizeof( MQTTContext_t ) );
    MQTTStatus_t mqttStatus = MQTT_Init(&mqttContext,
                                        &transportInterface,
                                        MQTT_GetCurrentTime,
										&mqttEventCallback,  // Optional callbacks
                                        &networkBuffer);
	if (mqttStatus != MQTTSuccess)
    {
        PRINTF("MQTT_Init failed with status: %d\r\n", mqttStatus);
        return;
    }

    result = xTaskCreate(mqtt_task, "mqtt_task", mqtt_task_STACK_DEPTH, NULL, MQTT_TASK_PRIORITY,
                         &s_mqttTaskHandle);
    if (pdPASS != result)
    {
    	PRINTF("mqtt_task create failed\r\n");
    	s_mqttTaskHandle = NULL;
    	return;
    }

    result = xTaskCreate(mqtt_heartbeat_task, "mqtt_heartbeat_task", 512, NULL, MQTT_TASK_PRIORITY,
                         &s_mqttHbTaskHandle);
    if (pdPASS != result)
    {
    	PRINTF("mqtt_heartbeat_task create failed\r\n");
    	s_mqttHbTaskHandle = NULL;
    }
}
