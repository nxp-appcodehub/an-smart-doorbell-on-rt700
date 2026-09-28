/*
 * Copyright 2024,2026 NXP
 *
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*${header:start}*/
#include "fsl_device_registers.h"

// #include "usb_host_config.h"
// #include "usb_host.h"
#include "fsl_device_registers.h"
#include "app.h"
#include "mcmgr.h"

#include "pin_mux.h"
// #include "usb_phy.h"
#include "clock_config.h"
#include "board.h"
#include "display_support.h"
#include "pmic_support.h"
#include "fsl_cache.h"

#define CPU_CLOCK_325_EN

/*${header:end}*/

// extern usb_host_handle g_HostHandle;

void BOARD_InitHardware(void)
{
    BOARD_ConfigMPU();
    BOARD_InitAHBSC();

    BOARD_InitBootPins();
    BOARD_InitUwbPins();
    BOARD_InitPsRamPins_Xspi2();

    BOARD_InitSAI1CodecPins();

	//Added for WIFI module support
    CLOCK_EnableClock(kCLOCK_Gpio3);
    CLOCK_EnableClock(kCLOCK_Gpio7);
    RESET_PeripheralReset(kGPIO3_RST_SHIFT_RSTn);
    RESET_PeripheralReset(kGPIO7_RST_SHIFT_RSTn);
    BOARD_InitPinsM2();

    BOARD_InitMipiPanelPinsEvk();

    CLOCK_EnableClock(kCLOCK_Gpio0);
    CLOCK_EnableClock(kCLOCK_Gpio1);
	RESET_PeripheralReset(kGPIO0_RST_SHIFT_RSTn);
    RESET_PeripheralReset(kGPIO1_RST_SHIFT_RSTn);

    RESET_ClearPeripheralReset(kGPIO0_RST_SHIFT_RSTn);

#ifdef CPU_CLOCK_325_EN
    BOARD_InitPmicPins();
    BOARD_InitPmic();
    BOARD_SetPmicVdd2Voltage(1100000U); /* 1.1v for 325MHz clock. */
    // Disable LDO
    POWER_SetVddnSupplySrc(kVddSrc_PMIC);
    POWER_SetVdd1SupplySrc(kVddSrc_PMIC);
    POWER_SetVdd2SupplySrc(kVddSrc_PMIC);
    POWER_ApplyPD();
    BOARD_BootClockHSRUN();
#else
    BOARD_BootClockRUN();
#endif

    BOARD_InitDebugConsole();
    BOARD_Init16bitsPsRam(XSPI2);

    /* Disable JPEG decoder power down. */
    POWER_DisablePD(kPDRUNCFG_PPD_JPEGDEC);
    POWER_DisablePD(kPDRUNCFG_APD_JPEGDEC);

    /* Disable NPU power down. */
    POWER_DisablePD(kPDRUNCFG_APD_NPU);
    POWER_DisablePD(kPDRUNCFG_PPD_NPU);

    /*Added for OSTIMER */
    CLOCK_AttachClk(kLPOSC_to_OSTIMER);
    CLOCK_SetClkDiv(kCLOCK_DivOstimerClk, 1U);

	/*Power on sense domain. */
	POWER_DisablePD(kPDRUNCFG_SHUT_SENSEP_MAINCLK);

    POWER_ApplyPD();
}


#ifdef CORE1_IMAGE_COPY_TO_RAM
uint32_t get_core1_image_size(void)
{
    uint32_t image_size;
#if defined(__CC_ARM) || defined(__ARMCC_VERSION)
    image_size = (uint32_t)&Image$$CORE1_REGION$$Length;
#elif defined(__ICCARM__)
    image_size = (uint32_t)__section_end("__core1_image") - (uint32_t)__section_begin("__core1_image");
#elif defined(__GNUC__)
    image_size = (uint32_t)core1_image_size;
#endif
    return image_size;
}
#endif /* CORE1_IMAGE_COPY_TO_RAM */

#ifdef APP_INVALIDATE_CACHE_FOR_SECONDARY_CORE_IMAGE_MEMORY
void invalidate_cache_for_core1_image_memory(uint32_t address, uint32_t size_byte)
{
    XCACHE_CleanInvalidateCacheByRange(address, size_byte);
}
#endif /* APP_INVALIDATE_CACHE_FOR_SECONDARY_CORE_IMAGE_MEMORY */

/*!
 * @brief Application-specific implementation of the SystemInitHook() weak function.
 */
void SystemInitHook(void)
{
    /* Initialize MCMGR - low level multicore management library. Call this
       function as close to the reset entry as possible to allow CoreUp event
       triggering. The SystemInitHook() weak function overloading is used in this
       application. */
    (void)MCMGR_EarlyInit();
}
/*${function:end}*/
