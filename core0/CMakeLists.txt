
# Copyright 2025 NXP
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.22.0)

# find_package(Mcuxsdk 25.09 EXACT REQUIRED)

include(
    ${SdkRootDirPath}/cmake/extension/mcux.cmake
)

project(
    doorbell
    # PROJECT_BOARD_PORT_PATH ${board_root}/${board}/demo_apps/hello_world
    LANGUAGES C CXX ASM
)

mcux_add_source(
    BASE_PATH ${CMAKE_CURRENT_LIST_DIR}/source
    SOURCES main.c
	# host_video.c
	# app.c
	wifi_task.c
	sdmmc_config.c
	shell_task.c
	# socket_task.c
	wifi_bt_config.c
	# pmic_support.c
	# pmic_support.h
	semihost_hardfault.c
	multicore.c
	mqtt_doorbell.c
)

mcux_add_source(
    BASE_PATH ${CMAKE_CURRENT_LIST_DIR}/source/display
    SOURCES  display_support.c
	         display_support.h
)


mcux_add_source(
   BASE_PATH ${CMAKE_CURRENT_LIST_DIR}/source/rtsp_server
   SOURCES rtsp_rtp_server.c
)

mcux_add_source(
    BASE_PATH ${CMAKE_CURRENT_LIST_DIR}/source/audio
    SOURCES  audio_task.c audio_jitter_buf.c
)

# Automatically find all .c files in uwb folder and subdirectories
file(GLOB_RECURSE UWB_SOURCES
         "${CMAKE_CURRENT_LIST_DIR}/source/uwb/*.c"
)

# Extract relative paths from the full paths.
# Exclusions:
#   halimpl/osal         - freertos/ provides FreeRTOS OSAL replacements; both
#                          together cause multiple-definition errors.
#   halimpl/fwdl_provider - firmware download provider is SR1XX-only; SR250
#                            uses a different path; including it causes
#                            unknown-type errors for uwb_fwdl_provider_t.
#   demos/common/vcom_common - USB VCOM / serial-comm layer (Zephyr/RW612 USB);
#                              not present on RT700 EVK doorbell build.
#   demos/common/pnp     - Plug-and-Play demo (UART/USB-based host tool);
#                          not used in the RT700 doorbell application.
set(UWB_RELATIVE_SOURCES "")
foreach(SOURCE ${UWB_SOURCES})
         if(SOURCE MATCHES "halimpl/osal")
             continue()
         endif()
         if(SOURCE MATCHES "halimpl/fwdl_provider")
             continue()
         endif()
         if(SOURCE MATCHES "demos/common/vcom_common")
             continue()
         endif()
         if(SOURCE MATCHES "demos/common/pnp")
             continue()
         endif()
         # Standalone_Main.c provides its own main() entry point for standalone
         # SDK demos. The doorbell project has its own main() in source/main.c;
         # including both causes a multiple-definition error at link time.
         if(SOURCE MATCHES "demos/common/Standalone_Main")
             continue()
         endif()
         # frdm_rw612 board files target Zephyr/RW612; exclude from RT700 doorbell build.
         if(SOURCE MATCHES "boards/Host/nxp/frdm_rw612")
             continue()
         endif()
         file(RELATIVE_PATH REL_SOURCE ${CMAKE_CURRENT_LIST_DIR}/source/uwb ${SOURCE})
         list(APPEND UWB_RELATIVE_SOURCES ${REL_SOURCE})
endforeach()

mcux_add_source(
         BASE_PATH ${CMAKE_CURRENT_LIST_DIR}/source/uwb
         SOURCES ${UWB_RELATIVE_SOURCES}
)

mcux_add_source(
    BASE_PATH ${CMAKE_CURRENT_LIST_DIR}/board
    SOURCES hardware_init.c
    # utick_config.c
    # utick_config.h
	# clock_config.c
	# clock_config.h
    #fsl_debug_console_conf.h
	pin_mux.c
	pin_mux.h
	# board.c
	# board.h
)

#### HEADERS SECTION ####


# Automatically find all include directories
file(GLOB_RECURSE UWB_HEADERS
         "${CMAKE_CURRENT_LIST_DIR}/source/uwb/*.h"
)

# Extract unique directories from header files.
# Exclude board-specific config dirs for boards OTHER than the target (rt700_evk),
# so that only one uwb_bus_board_config.h is on the include path.
set(UWB_INCLUDE_DIRS "")
foreach(HEADER ${UWB_HEADERS})
         get_filename_component(HEADER_DIR ${HEADER} DIRECTORY)
         # Skip the frdm_rw612 board directory - not the target for the doorbell project
         if(HEADER_DIR MATCHES "frdm_rw612")
             continue()
         endif()
         file(RELATIVE_PATH REL_DIR ${CMAKE_CURRENT_LIST_DIR} ${HEADER_DIR})
         list(APPEND UWB_INCLUDE_DIRS ${REL_DIR})
endforeach()
list(REMOVE_DUPLICATES UWB_INCLUDE_DIRS)

mcux_add_include(
         BASE_PATH ${CMAKE_CURRENT_LIST_DIR}
         INCLUDES  ${UWB_INCLUDE_DIRS}
)

mcux_add_include(
    BASE_PATH ${CMAKE_CURRENT_LIST_DIR}
    INCLUDES source board source/display  ../multicore
             source/rtsp_server  source/audio
)

mcux_add_source(
    BASE_PATH ${CMAKE_CURRENT_LIST_DIR}/source/mpp
    SOURCES mpp_config.h
            mpp_task.c
	        mpp_task.h
            mpp_app_task_notify.c
            mpp_app_task_notify.h
			event_listener.c
			app_types.h
			app_constants.h
)

mcux_add_include(
    BASE_PATH ${CMAKE_CURRENT_LIST_DIR}/source/mpp
    INCLUDES .
)

# include mpp cmake
include(
    ${CMAKE_CURRENT_LIST_DIR}/source/mpp/mpp.cmake
)

include(
    ${SdkRootDirPath}/CMakeLists.txt
)

mcux_add_include(
         BASE_PATH ${CMAKE_CURRENT_LIST_DIR}
         INCLUDES source
                  board
                  source/display
                  source/display/font
                  source/rtsp_server
                  source/uwb
                  ../multicore
)


include(
    ${CMAKE_CURRENT_LIST_DIR}/reconfig.cmake OPTIONAL
)

mcux_add_include(
    BASE_PATH ${APPLICATION_BINARY_DIR}
    TOOLCHAINS mdk armgcc
    INCLUDES ../
)

mcux_convert_binary(BINARY ${APPLICATION_BINARY_DIR}/${MCUX_SDK_PROJECT_NAME}.bin)
