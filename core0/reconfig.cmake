mcux_remove_armgcc_linker_script(
    BASE_PATH ${SdkRootDirPath}
    TARGETS flash_debug flash_release
    LINKER ${device_root}/RT/RT700/MIMXRT798S/gcc/MIMXRT798Sxxxx_cm33_core0_flash.ld
)

mcux_add_armgcc_linker_script(
    BASE_PATH ${CMAKE_CURRENT_LIST_DIR}
    TARGETS flash_debug flash_release
    LINKER core0_flash.ld
)


mcux_add_macro(
  CC "-DHAL_TIMER_PRECISION_1_US \
      -DPRINTF_ADVANCED_ENABLE=1 \
      -DVG_TARGET_FAST_CLEAR=0 \
      -DUSB_HOST_CONFIG_BUFFER_PROPERTY_CACHEABLE=1 \
      -DFSL_SDK_ENABLE_DRIVER_CACHE_CONTROL=1 \
      -DGCID_REV_CID=gc555/0x423_ECO \
      -DSDK_DEBUGCONSOLE=1 \
      -DSDIO_ENABLED=1 \
      -DCUSTOM_VGLITE_MEMORY_CONFIG=1 \
      -DCONFIG_COEX_APP \
      -DOSA_USED \
      -DUSB_STACK_FREERTOS \
      -DUSE_RTOS=1 \
      -D__FPU_PRESENT=1 \
      -DSERIAL_PORT_TYPE_UART=1 \
      -DDEBUG_CONSOLE_TRANSFER_NON_BLOCKING \
      -DSDK_OS_FREE_RTOS \
      -DSDK_I2C_BASED_COMPONENT_USED=1 \
      -DUWBIOT_USE_FTR_FILE=1 \
      -DARM_MATH_CM33 \
      -DBOARD_ENABLE_PSRAM_CACHE=0"

  CX "-DHAL_TIMER_PRECISION_1_US=1 \
      -D__FPU_PRESENT=1 \
      -DUSB_STACK_FREERTOS \
      -DARM_MATH_CM33 \
      -DFSL_SDK_ENABLE_DRIVER_CACHE_CONTROL=1 \
      -DDEBUG_CONSOLE_TRANSFER_NON_BLOCKING \
      -DDEBUG_CONSOLE_RX_ENABLE=0"
)

# needed to boot core 1
mcux_add_armgcc_configuration(
    CC "-DBOOT_SECONDARY_CORE \
        -DCORE1_IMAGE_COPY_TO_RAM "
)


# This file contain functions implemented multicore communication inside MPP
# but we have it implemented in project file multicore.c, so disable it
set_source_files_properties(
    ${SdkRootDirPath}/middleware/eiq/mpp/src/mpp_utils.c
    PROPERTIES HEADER_FILE_ONLY TRUE
)

# Needed to boot core1
mcux_add_armgcc_configuration(
    CC "-DMCMGR_USED "
)


# By default, O0 optimization, CPU loading is high, use -Og for better performance
mcux_add_armgcc_configuration(
    TARGETS flash_debug
    CC "-Og -g3"
    CX "-Og -g3"
)

mcux_add_armgcc_configuration(
    TARGETS flash_release
	CC "-O3"
    CX "-O3"
)

# Disable optimization for specific FreeRTOS files, or we can not see "RTOS DETAILS"
set(FREERTOS_DIR "${SdkRootDirPath}/rtos/freertos/freertos-kernel")
set_source_files_properties(
    ${FREERTOS_DIR}/tasks.c
    ${FREERTOS_DIR}/queue.c
    ${FREERTOS_DIR}/list.c
    PROPERTIES COMPILE_FLAGS "-O0 -g3"
)


mcux_add_armgcc_configuration(
        CC "-DCORE1_IMAGE_COPY_TO_RAM"
)

# this macro will disable all logging related to HALUCI commands
mcux_add_armgcc_configuration(
        CC "-DENABLE_HALUCI_CMD_LOGGING=0"
)

# UWB task stack overrides (bytes; divided by sizeof(StackType_t)=4 inside OSAL).
# CONFIG_NXP_UWB_STACK_SIZE reduced to 50% of the previous 4096-byte value -> 2048 bytes (512 words).
# CONFIG_NXP_UWB_READER_STACK_SIZE kept at 2048 bytes (512 words).
mcux_add_armgcc_configuration(
        CC "-DCONFIG_NXP_UWB_STACK_SIZE=2048 \
            -DCONFIG_NXP_UWB_READER_STACK_SIZE=2048"
)

# private share memory size 640K
mcux_add_linker_symbol(
    SYMBOLS "__stack_size__=0x4000 \
             __heap_size__=0x2000 \
             __use_shmem__=1 \
             __multicore__=1 \
             __rpmsg_shmem_size__=0x2000 \
             __prv_shmem_size__=819200 \
            "
)

# Used to update core0 image when there is any update on core1 image
set_source_files_properties(
#  ${CMAKE_CURRENT_SOURCE_DIR}/boards/mimxrt1170/startup_image.S
 ${SdkRootDirPath}/components/misc_utilities/fsl_incbin.S
 PROPERTIES OBJECT_DEPENDS "${APPLICATION_BINARY_DIR}/../core1_image.bin"
)