# UWB Middleware Port - Smart Doorbell (RT700 EVK B)

## Overview

This directory contains the NXP SR250 UWB IoT middleware ported to the
RT700 EVK B (MIMXRT798S) running FreeRTOS / MCUXpresso SDK.

The upstream SDK source is at:
`C:\workspace\github_sr250\sr250-uwbiot-zephyr\uwbiot-top`
(branch: vss_cmake_project)

Target hardware:
- Host MCU : RT700 EVK B (MIMXRT798S, core0 CM33)
- UWB module: ETNA-TS-250-DevKit connected on Arduino headers
  - SPI  : LP_FLEXCOMM1 / LPSPI1 (Arduino D10-D13)
  - RSTN : GPIO4_2  (Arduino D8)
  - IRQ  : GPIO4_3  (Arduino D9)

---

## Directory Structure

Mirrors the `uwbiot-top` upstream layout:

```
core0/source/uwb/
  libs/                           UWB middleware (halimpl, uci-core, uwb-iot)
  firmware_images/                SR250 firmware binaries
  demos/                          UWB demo applications
  boards/
    Host/nxp/
      rt700_evk/                  RT700 EVK B board-specific config
        uwb_bus_board_config.h    Pin/peripheral definitions
        include/                  Board application headers
          app_preinclude.h
          driver_config.h
          uwb_board.h
          uwb_bus_board.h
          uwb_iot_ftr.h
          UWB_Assert.h
          UWB_DeviceConfig.h
          UWBIOT_APP_BUILD.h
      frdm_rw612/                 FRDM-RW612 board config (reference, not used)
        uwb_bus_board_config.h
    uwbs_driver/                  UWB bus interface header (from upstream)
      uwb_bus_interface.h
  freertos/                       FreeRTOS OSAL layer
    phOsalUwb_FreeRTOS.c
    phOsalUwb_Queue_FreeRTOS.c
    phOsalUwb_Thread_FreeRTOS.c
    phOsalUwb_Timer_FreeRTOS.c
  uwb_app.c                       Application entry point (calls uwb_init)
  uwb_app.h
```

---

## Modifications vs Upstream SDK

### 1. `freertos/phOsalUwb_Thread_FreeRTOS.c`
**Upstream:** `libs/halimpl/osal/phOsalUwb_Thread_FreeRTOS.c`

- Stores `phOsalUwb_sOsalThreadHandle_t` wrapper (with `.ObjectHandle` TaskHandle_t)
  instead of raw `TaskHandle_t` in `*hThread`. Required because the middleware
  passes the handle to Delete/Join/Suspend/Resume which expect the wrapper struct.
- `phOsalUwb_Thread_Delete`: casts to wrapper, calls `vTaskDelete(wrapper->ObjectHandle)`,
  frees wrapper memory.
- `phOsalUwb_Thread_Join`: polls `wrapper->ObjectHandle != NULL` instead of
  `eTaskGetState(hThread) != eDeleted`.
- `phOsalUwb_TaskResume`: ISR-safe - calls `xTaskResumeFromISR` when in ISR.
- `phOsalUwb_Thread_Context_Switch`: skips `taskYIELD()` when in ISR.
- Added `phOsalUwb_GetTaskHandle()` - allocates wrapper for current task.
- Added ISR detection via CMSIS `SCB->ICSR` (`uwb_thread_is_in_isr()`).
- Added ISR guard on `phOsalUwb_Thread_Create` - returns `UWBSTATUS_PENDING` if in ISR.
- Stack depth: divides `pThreadParams->stackdepth` by `sizeof(StackType_t)` (input
  is bytes; FreeRTOS `xTaskCreate` takes words).
- Priority: clamped to `configMAX_PRIORITIES - 1`; falls back to
  `UWB_DEFAULT_TASK_PRIORITY` if 0.

### 2. `freertos/phOsalUwb_Timer_FreeRTOS.c`
**Upstream:** `libs/halimpl/osal/phOsalUwb_Timer_FreeRTOS.c`

- `phOsalUwb_Timer_Create` parameter type: upstream latest changed from
  `uint8_t bAutoReload` to `bool bAutoReload`; our implementation now matches.
- Removed `pTimerHandle->bAutoReload = bAutoReload` - field does not exist in
  RT700 SDK `phOsalUwb_TimerHandle_t` struct.
- `phOsalUwb_Timer_Expired` callback: always sets `eState = eTimerStopped`
  (auto-reload is managed by FreeRTOS, not tracked in the handle struct).
- `phOsalUwb_Timer_Start`: clamps `ticks` to minimum 1 if `pdMS_TO_TICKS` returns 0.
- ISR detection uses `cmsis_compiler.h` instead of `fsl_common.h`.
- Timer name uses static `"uwbtmr"` instead of `snprintf` formatted string.
- `phOsalUwb_Timer_Stop` error code: uses `UWBSTATUS_INVALID_PARAMETER` instead
  of `PH_OSALUWB_TIMER_STOP_ERROR`.

### 3. `boards/Host/nxp/freertos_common/uwb_bus_interface_freertos.c`
**Upstream:** `boards/Host/nxp/freertos_common/uwb_bus_interface_freertos.c`

- Uses explicit `#if UWB_USE_LPSPI` / `#else` blocks instead of top-of-file
  macro aliases (`UWB_SPI_HANDLE_TYPE`, `uwb_spi_init_fn`, etc.).
- `LP_FLEXCOMM_Init` call wrapped in `#if defined(UWB_LPFLEXCOMM_IDX)` guard
  for portability to boards without LP_FLEXCOMM.
- LPSPI config: removed timing delay fields (`pcsToSckDelayInNanoSec` etc.);
  added `bitsPerFrame=8U`, `pcsActiveHighOrLow=kLPSPI_PcsActiveLow`,
  `pinCfg=kLPSPI_SdiInSdoOut`, `dataOutConfig=kLpspiDataOutRetained`.
- Removed `enableContinuousSCK` - field does not exist in RT700
  `lpspi_master_config_t`.
- SPI callback: `s_transferStatus = (status == kStatus_Success) ? 0 : -1`
  instead of direct cast.
- `masterHandle` assigned at start of `uwb_bus_init` before SPI init.
- Transfer helper uses typed `lpspi_transfer_t*` / `spi_transfer_t*` parameter.
- RX direction byte uses `static uint8_t s_dirByte` instead of stack buffer.
- `uwb_bus_deinit`: SPI abort + deinit before semaphore cleanup.
- `uwb_port_DelayinMicroSec`: uses `CLOCK_GetCoreSysClkFreq()` instead of
  `SDK_DEVICE_MAXIMUM_CPU_CLOCK_FREQUENCY`.

### 4. `boards/Host/nxp/freertos_common/uwb_bus_io_freertos.c`
**Upstream:** `boards/Host/nxp/freertos_common/uwb_bus_io_freertos.c`

- All GPIO API calls use the RT700 MCUXpresso SDK 3-argument form
  (no port index parameter):
  - `GPIO_PinInit(BASE, PIN, &cfg)` instead of `GPIO_PinInit(BASE, PORT_IDX, PIN, &cfg)`
  - `GPIO_PinWrite(BASE, PIN, val)` instead of `GPIO_PinWrite(BASE, PORT_IDX, PIN, val)`
  - `GPIO_PinRead(BASE, PIN)` instead of `GPIO_PinRead(BASE, PORT_IDX, PIN)`
  - `GPIO_SetPinInterruptConfig(BASE, PIN, edge)` instead of 4-arg form
- IRQ clear uses `GPIO_PinClearInterruptFlag(BASE, PIN)` instead of
  `GPIO_GpioClearInterruptFlags(BASE, PORT_IDX, 1UL << PIN)`.
- Two-layer ISR structure: static `UWB_HELIOS_IRQ_HANDLER` dispatcher +
  `UWB_GPIO_IRQ_HANDLER` wrapper (vs. single function in upstream).
- Callback array renamed `s_mCallbacks` (static prefix).
- `uwb_bus_io_deinit` uses `memset` instead of `phOsalUwb_SetMemory`.
- `uwb_bus_io_uwbs_irq_disable` removed (not required by doorbell application).

### 5. `boards/Host/nxp/rt700_evk/uwb_bus_board_config.h`
**Upstream:** `boards/Host/nxp/rt700_evk/uwb_bus_board_config.h`

| Define | Upstream value | Doorbell value | Reason |
|---|---|---|---|
| `UWB_SPI_CLK_ATTACH` | `kFRO1_DIV4_to_LPFLEXCOMM1` | `kFRO0_DIV1_to_FCCLK1` | Only valid clock attach ID on RT700 EVK B SDK |
| `UWB_GPIO_IRQ` | `GPIO4_IRQn` | `GPIO40_IRQn` | Correct NVIC vector for GPIO4 port 0 on RT700 EVK B |
| `UWB_GPIO_IRQ_HANDLER` | `GPIO4_IRQHandler` | `GPIO40_IRQHandler` | Matching vector table symbol |
| `MAX_UWBS_SPI_TRANSFER_TIMEOUT` | redefined here | removed | Already defined in `uwb_bus_interface.h`; duplicate causes `-Werror` |

Header comment updated to document ETNA-TS-250-DevKit hardware and Arduino pin mapping.

### 6. `core0/CMakeLists.txt`
**Upstream:** N/A (doorbell project build system, not from upstream SDK)

Two filter loops added to the `GLOB_RECURSE` source/include scanning:

1. **`halimpl/osal` source exclusion** (prevents multiple-definition linker errors
   if upstream osal `.c` files are present alongside `freertos/` copies):
   ```cmake
   if(SOURCE MATCHES "halimpl/osal")
       continue()
   endif()
   ```

2. **`frdm_rw612` include-dir exclusion** (ensures only `rt700_evk`
   `uwb_bus_board_config.h` is on the include path):
   ```cmake
   if(HEADER_DIR MATCHES "frdm_rw612")
       continue()
   endif()
   ```

Additional exclusions added after upstream sync (libs/firmware_images/demos updated
to latest):

3. **`halimpl/fwdl_provider` source exclusion** - firmware download provider is
   SR1XX-only; SR250 uses a different path. Including it caused
   `unknown type name 'uwb_fwdl_provider_t'` errors.

4. **`demos/common/vcom_common` source exclusion** - USB VCOM / serial
   communication layer targeting Zephyr / RW612 USB. Not available on RT700 EVK B.
   Caused `implicit declaration of function 'UWB_Serial_Com_Init'` errors.

5. **`demos/common/pnp` source exclusion** - Plug-and-Play demo using UART/USB
   host tool interface. Caused `implicit declaration of function 'AddDelayInMicroSec'`
   errors (via `UWB_Spi_Driver_Interface_Sr1xx_Sr2xx.c`).

6. **`demos/common/Standalone_Main` source exclusion** - upstream demo entry point
   that defines its own `main()` and `testTaskHandle`. Conflicts with the doorbell
   `source/main.c` and `uwb_app.c` definitions.

### 7. `boards/Host/nxp/rt700_evk/include/uwb_board.h`
**Upstream:** N/A (doorbell-specific board header)

Added after upstream sync:
- `#include "fsl_debug_console.h"` and `#include "fsl_common.h"` to pull in NXP
  SDK debug console and `SDK_DelayAtLeastUs`.
- `#ifndef PRINTF` / `#define PRINTF DbgConsole_Printf` - `uwb_logging.h`
  requires `PRINTF` to be defined before it is included; the NXP SDK maps it
  through `fsl_debug_console.h`.
- `AddDelayInMicroSec(int delay)` inline function - busy-wait helper required by
  `libs/halimpl/tml/phTmlUwb_transport.c`. Maps to `SDK_DelayAtLeastUs`.

### 8. `uwb_app.c`
**Upstream:** N/A (doorbell application file)

Added `QueueHandle_t shellCmdQueue` definition. This queue is declared `extern` in
`core0/source/shell_task.c` and used to receive UWB sensitivity commands from the
shell. The queue was previously defined in `demos/common/Standalone_Main.c` which is
now excluded from the build.

---

## Directory Structure Changes

The original layout was:

```
core0/source/uwb/
  freertos_port/
    board/common/      <- board drivers
    board/rt700_evk/   <- board config
    board/frdm_rw612/  <- board config
    osal/              <- FreeRTOS OSAL
  Host/                <- board application headers
  uwb-sr250/
    libs/
    firmware_images/
    demos/
```

Reorganized to mirror `uwbiot-top`, then further consolidated (freertos_common
merged into freertos/):

```
core0/source/uwb/
  boards/Host/nxp/
    rt700_evk/         <- was freertos_port/board/rt700_evk/
      include/         <- was Host/
    frdm_rw612/        <- was freertos_port/board/frdm_rw612/ (kept, excluded from build)
    uwbs_driver/       <- copied from upstream boards/uwbs_driver/
  freertos/            <- was freertos_port/osal/ + freertos_port/board/common/
                          (uwb_bus_interface_freertos.c and uwb_bus_io_freertos.c
                           moved here from boards/Host/nxp/freertos_common/)
  libs/                <- was uwb-sr250/libs/
  firmware_images/     <- was uwb-sr250/firmware_images/
  demos/               <- was uwb-sr250/demos/
```

---

## Firmware

Active firmware: `firmware_images/SR2XX/Prod_ROW/SR200A1V2_IOT_01.41.04_Prod.bin`
(upstream production release, replaces older `SR200A1V2_01.31.C3_customer.bin`)

---

## Build

Build system: MCUXpresso SDK CMake + ninja (sysbuild)
Build directory: `flash_debug/`

```
ninja -C flash_debug -j8
```
