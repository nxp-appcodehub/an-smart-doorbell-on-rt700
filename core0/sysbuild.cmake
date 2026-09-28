
# Copyright 2025 NXP
#
# SPDX-License-Identifier: Apache-2.0



ExternalMCUXProject_Add(
    APPLICATION core1
    SOURCE_DIR ${APP_DIR}/../core1
    board ${SB_CONFIG_secondary_board}
    core_id ${SB_CONFIG_secondary_core_id}
    config ${SB_CONFIG_secondary_config}
    toolchain ${SB_CONFIG_secondary_toolchain}
)

add_dependencies(${DEFAULT_IMAGE} core1)