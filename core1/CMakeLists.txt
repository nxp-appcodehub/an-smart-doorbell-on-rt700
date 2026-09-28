
# Copyright 2025 NXP
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.22.0)

include(
    ${SdkRootDirPath}/cmake/extension/mcux.cmake
)

project(
    doorbell
    # PROJECT_BOARD_PORT_PATH ${board_root}/${board}/multicore_examples/multicore_manager
    LANGUAGES C CXX ASM
)

mcux_add_include(
    BASE_PATH ${SdkRootDirPath}/middleware/eiq/mpp
    INCLUDES    hal/include
)

include(
    ${SdkRootDirPath}/CMakeLists.txt
)

mcux_add_source(
    BASE_PATH ${CMAKE_CURRENT_LIST_DIR}/source
    SOURCES main.c
    app.c
	host_video.c
	# multicore.c
)

mcux_add_source(
    BASE_PATH ${CMAKE_CURRENT_LIST_DIR}/board
    SOURCES hardware_init.c
)

mcux_add_include(
    BASE_PATH ${CMAKE_CURRENT_LIST_DIR}
    INCLUDES . source board ../multicore
)
mcux_add_configuration(
    CC "-D__USE_SHMEM -DMCMGR_HANDLE_EXCEPTIONS -D__SEMIHOST_HARDFAULT_DISABLE"
)

mcux_add_mdk_configuration(
    LD "--library_type=standardlib"
)

include(
    ${CMAKE_CURRENT_LIST_DIR}/reconfig.cmake OPTIONAL
)

mcux_convert_binary(
    TOOLCHAINS armgcc
    BINARY ${APPLICATION_BINARY_DIR}/../core1_image.bin
	# BINARY ${APPLICATION_BINARY_DIR}/${CONFIG_TOOLCHAIN}/core1_image.bin
)