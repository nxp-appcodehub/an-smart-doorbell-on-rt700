/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#if defined(configGENERATE_RUN_TIME_STATS) && (configGENERATE_RUN_TIME_STATS == 1)
#define configRUN_TIME_COUNTER_TYPE uint64_t
void TIMER_Init(void);
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS 	TIMER_Init

extern configRUN_TIME_COUNTER_TYPE TIMER_GetTimeInUS(void);
#define portGET_RUN_TIME_COUNTER_VALUE			TIMER_GetTimeInUS
#endif