mcux_remove_armgcc_linker_script(
    BASE_PATH ${SdkRootDirPath}
    LINKER ${device_root}/RT/RT700/MIMXRT798S/gcc/MIMXRT798Sxxxx_cm33_core1_ram.ld
)

mcux_add_armgcc_linker_script(
    BASE_PATH ${CMAKE_CURRENT_LIST_DIR}
    LINKER core1_ram.ld
)

# OSA_USED  used in list, serial manager, debug console and shell
# USE_RTOS  used in list, osa, usb and shell.
# SDK_OS_FREE_RTOS  used in gpio adapter, osa, uart adapter, debug console, shell and video , wifi driver
# mcux_add_macro(
#   CC "-DPRINTF_ADVANCED_ENABLE=1 \
#       -DSDK_DEBUGCONSOLE=1 \
#       -DOSA_USED \
#       -DUSE_RTOS=1 \
#       -DSERIAL_PORT_TYPE_UART=1 \
#       -DDEBUG_CONSOLE_TRANSFER_NON_BLOCKING \
#       -DSDK_OS_FREE_RTOS"
# )

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

# needed to boot core 1
mcux_add_armgcc_configuration(
    CC "-DMCMGR_USED "
)

mcux_add_linker_symbol(
    SYMBOLS "__stack_size__=0x4000 \
             __heap_size__=0x2000 \
             __rpmsg_shmem_size__=0x2000 \
             __prv_shmem_size__=819200 \
            "
)
