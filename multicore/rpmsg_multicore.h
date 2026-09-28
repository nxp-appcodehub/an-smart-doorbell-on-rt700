/*
 * Copyright 2025 NXP.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file rpmsg_multicore.h
 * @brief RPMsg-Lite configuration macros shared between core0 and core1.
 *
 * This header centralizes RPMsg link ID and handshake event data constants
 * that are specific to the inter-core communication layer and are not part
 * of the HAL camera interface.
 */

#ifndef _RPMSG_MULTICORE_H_
#define _RPMSG_MULTICORE_H_

#include "rpmsg_platform.h"

/** @brief RPMsg-Lite link ID for the M33_0 <-> M33_1 channel on i.MX RT700. */
#define RPMSG_LITE_LINK_ID            (RL_PLATFORM_IMXRT700_M33_0_M33_1_LINK_ID)

/** @brief RPMsg-Lite name-service announce string for the virtual camera channel. */
#define RPMSG_LITE_NS_ANNOUNCE_STRING "rpmsg-virtual-camera-channel"

/** @brief Event data value signalling that the remote RPMsg stack is ready. */
#define APP_RPMSG_READY_EVENT_DATA    (1U)


#endif /* _RPMSG_MULTICORE_H_ */
