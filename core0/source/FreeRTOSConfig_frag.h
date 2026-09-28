/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#if defined(configGENERATE_RUN_TIME_STATS) && (configGENERATE_RUN_TIME_STATS == 1)

/* Switch to OSTIMER after MPP confirm that OSTIMER is preferred,
 * by this way, we can decrease CPU loading by removing the utick's ISR
 * which is called every 100us.
 */
#if 0
extern void vConfigureTimerForRunTimeStats(void);
extern unsigned long vGetTimerForRunTimeStats(void);
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS 	vConfigureTimerForRunTimeStats
#define portGET_RUN_TIME_COUNTER_VALUE			vGetTimerForRunTimeStats
#else
#define configRUN_TIME_COUNTER_TYPE uint64_t
extern void TIMER_Init(void);
extern configRUN_TIME_COUNTER_TYPE TIMER_GetTimeInUS(void);
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS 	TIMER_Init
#define portGET_RUN_TIME_COUNTER_VALUE			TIMER_GetTimeInUS
#endif

#endif