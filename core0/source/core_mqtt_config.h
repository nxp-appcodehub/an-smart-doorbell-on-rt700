/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * core_mqtt_config.h
 *
 *  Created on: 28-Sep-2025
 *      Author: Akshay
 */

#ifndef CORE_MQTT_CONFIG_H_
#define CORE_MQTT_CONFIG_H_

/* Logging configuration */
#define LIBRARY_LOG_NAME     "CoreMQTT"
#define LIBRARY_LOG_LEVEL    LOG_INFO

/* Enable or disable asserts */
#define MQTT_DO_ASSERT       1

/* Maximum number of subscriptions maintained */
#define MQTT_MAX_NUM_SUBSCRIPTIONS    10

/* Maximum number of outgoing publishes waiting for acknowledgment */
#define MQTT_MAX_OUTGOING_PUBLISHES   5

/* Maximum number of pending acknowledgments for SUBSCRIBE and UNSUBSCRIBE */
#define MQTT_MAX_PENDING_ACKS         5

/* Enable support for MQTT 5.0 properties (set to 0 for MQTT 3.1.1 only) */
#define MQTT_SUPPORTS_CLIENT_ID       1
#define MQTT_SUPPORTS_USER_NAME       1
#define MQTT_SUPPORTS_PASSWORD        1

/* Enable keep-alive mechanism */
#define MQTT_KEEP_ALIVE_INTERVAL_SECONDS   60

/* Enable clean session by default */
#define MQTT_CLEAN_SESSION                 1

/* Enable support for QoS 1 and QoS 2 */
#define MQTT_ENABLE_QOS1                   1
#define MQTT_ENABLE_QOS2                   0  // Set to 1 if needed

/* Enable support for MQTT ping response timeout */
#define MQTT_PINGRESP_TIMEOUT_MS           5000

/* Enable support for MQTT receive timeout */
#define MQTT_RECV_TIMEOUT_MS               5000

/* Enable support for MQTT send timeout */
#define MQTT_SEND_TIMEOUT_MS               5000

/* Enable support for MQTT serialization buffer size */
#define MQTT_PACKET_BUFFER_SIZE            1024

#endif /* CORE_MQTT_CONFIG_H_ */
