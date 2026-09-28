/*
 *  Copyright 2020-2021 NXP
 *
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _APP_PREINCLUDE_H_
#define _APP_PREINCLUDE_H_

#define USB_HOST_CONFIG_EHCI (1U)
#define CONTROLLER_ID        kUSB_ControllerEhci0

#define CONFIG_BT_PERIPHERAL            1
#define CONFIG_BT_DEVICE_NAME           "NXP_SR250"
#define CONFIG_BT_SMP                   1
#define CONFIG_BT_SETTINGS              1
#define CONFIG_BT_HOST_CRYPTO           1
#define CONFIG_BT_KEYS_OVERWRITE_OLDEST 1
#define CONFIG_BT_PAIRING               0
#define CONFIG_BT_MAX_CONN              5

#if CONFIG_BT_PAIRING
    #define CONFIG_BT_FIXED_PASSKEY         1
    #define FIXED_PASSKEY_VALUE             999999
#else
    /* set Legacy Pairing to enable JUST_WORKS pairing method and disable the CONFIG_BT_ECC used for secure connection */
    #define CONFIG_BT_SMP_OOB_LEGACY_PAIR_ONLY 1
#endif // CONFIG_BT_PAIRING

#include "edgefast_bluetooth_config.h"

/* Enable/Disable sensors module for temperature measurements */
#define APP_USE_SENSORS      0

/* Enable/Disable low power entry on tickless idle */
#define APP_LOWPOWER_ENABLED 0

#if defined(APP_LOWPOWER_ENABLED) && (APP_LOWPOWER_ENABLED > 0)
/* If low power is enabled, force tickless idle enable in FreeRTOS */
#define configUSE_TICKLESS_IDLE 1
#endif
#endif /* _APP_PREINCLUDE_H_ */
