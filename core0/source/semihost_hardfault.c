// ****************************************************************************
// semihost_hardfault.c
//   - Provides a hard fault handler that:
//       1. Allows semihosting BKPT instructions to continue when no debugger
//          is connected (original NXP behavior).
//       2. For all other hard faults, dumps the CPU state (stacked registers,
//          fault-status registers) directly to LPUART0 using polled TX,
//          bypassing PRINTF / DMA / FreeRTOS entirely. This is safe even when
//          the OS scheduler, DMA engine, or interrupt system is broken.
//
// ****************************************************************************
// Copyright 2017-2025 NXP
//
// SPDX-License-Identifier: BSD-3-Clause
// ****************************************************************************
//
// HOW TO FIND THE FAULTING LINE OF CODE
// ======================================
//
// When a hard fault fires this handler prints on the serial console:
//
//   *** HARD FAULT ***
//   Task : uwb_task
//   R0   = 0x00000000
//   R1   = 0x00000001
//   R2   = 0x00000002
//   R3   = 0x00000003
//   R12  = 0x00000000
//   LR   = 0x080012AB
//   PC   = 0x08001234   <-- faulting instruction address
//   xPSR = 0x21000000
//   HFSR = 0x40000000
//   CFSR = 0x00000200   <-- decode this to understand the fault type
//   MMFAR= 0xDEADBEEF   <-- bad address (MemManage / BusFault)
//   BFAR = 0xDEADBEEF
//
// Step 1 - translate PC to source line:
//   arm-none-eabi-addr2line -e <app>.elf -a 0x08001234 -f -p
//
// Step 2 - translate LR (strip the Thumb bit if set) to see the caller:
//   arm-none-eabi-addr2line -e <app>.elf -a 0x080012AA -f -p
//
// Step 3 - decode CFSR bits:
//   Bits [7:0]   MMFSR (MemManage):
//     bit 0 IACCVIOL  : instruction access violation
//     bit 1 DACCVIOL  : data access violation (MMFAR = bad address)
//     bit 3 MUNSTKERR : fault on exception return unstacking
//     bit 4 MSTKERR   : fault on exception entry stacking
//     bit 7 MMARVALID : MMFAR contains a valid address
//   Bits [15:8]  BFSR (BusFault):
//     bit 0 IBUSERR    : instruction bus error
//     bit 1 PRECISERR  : precise data bus error (BFAR = bad address)
//     bit 2 IMPRECISERR: imprecise data bus error
//     bit 3 UNSTKERR   : fault on exception return unstacking
//     bit 4 STKERR     : fault on exception entry stacking
//     bit 7 BFARVALID  : BFAR contains a valid address
//   Bits [31:16] UFSR (UsageFault):
//     bit 0 UNDEFINSTR : undefined instruction
//     bit 1 INVSTATE   : invalid EPSR state (bad function pointer)
//     bit 2 INVPC      : invalid PC on EXC_RETURN
//     bit 3 NOCP       : no coprocessor
//     bit 8 UNALIGNED  : unaligned access
//     bit 9 DIVBYZERO  : divide by zero
//
// WHY NOT PRINTF?
//   PRINTF on this SDK uses an interrupt-driven ring buffer protected by a
//   FreeRTOS mutex. If the fault was caused by a stack overflow, bad pointer,
//   or FreeRTOS heap corruption any of those may already be broken. This
//   handler writes directly to the LPUART0 TX data register in polled mode
//   with no OS calls, no DMA, and no interrupts.
//
// ****************************************************************************

#include <stdint.h>
// fsl_device_registers.h provides LPUART_Type and the LPUART0 base pointer.
#include "fsl_device_registers.h"
// FreeRTOS task.h provides pcTaskGetName(). Reading pxCurrentTCB->pcTaskName
// is a single pointer dereference - safe in a fault handler.
#include "FreeRTOS.h"
#include "task.h"

// ---------------------------------------------------------------------------
// Polled UART output - direct register access, no SDK, no RTOS, no DMA.
//
// Core0 debug console = LPUART0 (BOARD_DEBUG_UART_BASEADDR in board.h).
// LPUART STAT register bit 23 = TDRE (Transmit Data Register Empty).
// ---------------------------------------------------------------------------
#define HF_UART         LPUART0
#define HF_UART_TDRE    (1UL << 23)

static void hf_putchar(char c)
{
    // Wait until the TX FIFO has room, then write one byte.
    while (!(HF_UART->STAT & HF_UART_TDRE))
    {
        __asm volatile ("NOP");
    }
    HF_UART->DATA = (uint32_t)(uint8_t)c;
}

static void hf_puts(const char *s)
{
    while (*s)
    {
        hf_putchar(*s++);
    }
}

// Print a 32-bit value as 8 hex digits, prefixed with "0x".
static void hf_puthex32(uint32_t val)
{
    static const char hex[] = "0123456789ABCDEF";
    hf_putchar('0');
    hf_putchar('x');
    for (int shift = 28; shift >= 0; shift -= 4)
    {
        hf_putchar(hex[(val >> shift) & 0xFu]);
    }
}

// ---------------------------------------------------------------------------
// ARM Cortex-M fault status registers (System Control Block)
// ---------------------------------------------------------------------------
#define SCB_HFSR   (*((volatile uint32_t *)0xE000ED2CU))
#define SCB_CFSR   (*((volatile uint32_t *)0xE000ED28U))
#define SCB_MMFAR  (*((volatile uint32_t *)0xE000ED34U))
#define SCB_BFAR   (*((volatile uint32_t *)0xE000ED38U))

// ---------------------------------------------------------------------------
// Fault dump - called from the naked assembly trampoline.
//
// Exception frame layout (Cortex-M automatic stacking):
//   frame[0] = R0
//   frame[1] = R1
//   frame[2] = R2
//   frame[3] = R3
//   frame[4] = R12
//   frame[5] = LR   (return address of the code that faulted)
//   frame[6] = PC   (address of the faulting instruction)
//   frame[7] = xPSR
// ---------------------------------------------------------------------------
void prvHardFaultDump(uint32_t *frame)
{
    uint32_t pc   = frame[6];
    uint32_t hfsr = SCB_HFSR;
    uint32_t cfsr = SCB_CFSR;
    uint32_t mmfar = SCB_MMFAR;
    uint32_t bfar  = SCB_BFAR;

    hf_puts("\r\n*** HARD FAULT ***\r\n");

    // pcTaskGetName(NULL) reads pxCurrentTCB->pcTaskName - safe here.
    hf_puts("Task : ");
    hf_puts(pcTaskGetName(NULL));
    hf_puts("\r\n");

    hf_puts("R0   = "); hf_puthex32(frame[0]); hf_puts("\r\n");
    hf_puts("R1   = "); hf_puthex32(frame[1]); hf_puts("\r\n");
    hf_puts("R2   = "); hf_puthex32(frame[2]); hf_puts("\r\n");
    hf_puts("R3   = "); hf_puthex32(frame[3]); hf_puts("\r\n");
    hf_puts("R12  = "); hf_puthex32(frame[4]); hf_puts("\r\n");
    hf_puts("LR   = "); hf_puthex32(frame[5]); hf_puts("\r\n");
    hf_puts("PC   = "); hf_puthex32(pc);        hf_puts("  <-- faulting instruction\r\n");
    hf_puts("xPSR = "); hf_puthex32(frame[7]); hf_puts("\r\n");

    hf_puts("HFSR = "); hf_puthex32(hfsr);  hf_puts("\r\n");
    hf_puts("CFSR = "); hf_puthex32(cfsr);  hf_puts("\r\n");
    hf_puts("MMFAR= "); hf_puthex32(mmfar); hf_puts("\r\n");
    hf_puts("BFAR = "); hf_puthex32(bfar);  hf_puts("\r\n");

    hf_puts("\r\nRun: arm-none-eabi-addr2line -e <app>.elf -a ");
    hf_puthex32(pc);
    hf_puts(" -f -p\r\n");

    // Spin - a JTAG/SWD debugger can still attach here.
    for (;;)
    {
        __asm volatile ("NOP");
    }
}

// ---------------------------------------------------------------------------
// Hard fault entry trampoline (naked - no compiler-generated prologue)
// ---------------------------------------------------------------------------
#if !defined(__SEMIHOST_HARDFAULT_DISABLE)

__attribute__((naked))
void HardFault_Handler(void)
{
    __asm volatile (
        ".syntax unified                \n"

        // Determine whether we faulted from MSP (main stack) or PSP (task
        // stack) and load the stacked exception frame base address into R0.
        "MOVS   R0, #4                  \n"
        "MOV    R1, LR                  \n"
        "TST    R0, R1                  \n"
        "BEQ    _msp_frame              \n"
        "MRS    R0, PSP                 \n"
        "B      _check_semihost         \n"
        "_msp_frame:                    \n"
        "MRS    R0, MSP                 \n"

        // Semihosting check: load the 16-bit instruction at the stacked PC.
        // frame[6] is at offset 24 from the frame base.
        // If it is BKPT 0xAB (opcode 0xBEAB) this is a semihosting call with
        // no debugger attached; skip past it and return to the application.
        "_check_semihost:               \n"
        "LDR    R1, [R0, #24]           \n"  // stacked PC
        "LDRH   R2, [R1]                \n"  // 16-bit instruction at that PC
        "LDR    R3, =0xBEAB             \n"  // BKPT 0xAB opcode
        "CMP    R2, R3                  \n"
        "BEQ    _semihost_return        \n"

        // Real fault: tail-call the C dump function. R0 already holds the
        // frame pointer - it is the first argument per AAPCS.
        "B      prvHardFaultDump        \n"

        // Semihosting return: advance stacked PC past the BKPT (+2 bytes)
        // and set the semihosting return value in stacked R0.
        "_semihost_return:              \n"
        "ADDS   R1, #2                  \n"
        "STR    R1, [R0, #24]           \n"
        "LDR    R1, [R0, #0]            \n"  // stacked R0 = semihosting op
        "CMP    R1, #1                  \n"  // 0x01 = SYS_OPEN
        "BEQ    _non_zero_ret           \n"
        "MOVS   R1, #0                  \n"
        "B      _sys_ret                \n"
        "_non_zero_ret:                 \n"
        "MOVS   R1, #32                 \n"
        "_sys_ret:                      \n"
        "STR    R1, [R0, #0]            \n"
        "BX     LR                      \n"
        ".syntax divided                \n"
    );
}

#endif // !defined(__SEMIHOST_HARDFAULT_DISABLE)
