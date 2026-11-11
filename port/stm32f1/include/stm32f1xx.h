/*
 * Minimal replacement for the vendor device header, providing just what the
 * TinyUSB FSDEV driver needs on an STM32F103. Written from the reference
 * manual register map; no vendor code is used.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef HYBRIDLINK_STM32F1XX_SHIM_H
#define HYBRIDLINK_STM32F1XX_SHIM_H

#include <stdint.h>

typedef enum {
    USB_HP_CAN1_TX_IRQn  = 19,
    USB_LP_CAN1_RX0_IRQn = 20,
    USBWakeUp_IRQn       = 42,
} IRQn_Type;

#define USB_BASE        0x40005C00UL
#define USB_PMAADDR     0x40006000UL

static inline void __DSB(void) { __asm volatile ("dsb 0xF" ::: "memory"); }
static inline void __ISB(void) { __asm volatile ("isb 0xF" ::: "memory"); }

#define NVIC_ISER_(n)   (*(volatile uint32_t *)(0xE000E100UL + 4UL * (n)))
#define NVIC_ICER_(n)   (*(volatile uint32_t *)(0xE000E180UL + 4UL * (n)))

static inline void NVIC_EnableIRQ(IRQn_Type irq)
{
    NVIC_ISER_((uint32_t)irq >> 5) = 1UL << ((uint32_t)irq & 31u);
}

static inline void NVIC_DisableIRQ(IRQn_Type irq)
{
    NVIC_ICER_((uint32_t)irq >> 5) = 1UL << ((uint32_t)irq & 31u);
    __DSB();
    __ISB();
}

#endif
