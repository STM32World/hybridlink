/*
 * Cortex-M3 startup for STM32F103 (vector table, .data/.bss init).
 * SPDX-License-Identifier: MIT
 */
#include <stdint.h>
#include "layout.h"

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
extern int main(void);

void Reset_Handler(void);
void Default_Handler(void);

#define WEAK __attribute__((weak, alias("Default_Handler")))
void NMI_Handler(void) WEAK;
void HardFault_Handler(void) WEAK;
void MemManage_Handler(void) WEAK;
void BusFault_Handler(void) WEAK;
void UsageFault_Handler(void) WEAK;
void SVC_Handler(void) WEAK;
void DebugMon_Handler(void) WEAK;
void PendSV_Handler(void) WEAK;
void SysTick_Handler(void) WEAK;
void DMA1_Channel6_IRQHandler(void) WEAK;
void DMA1_Channel7_IRQHandler(void) WEAK;
void USB_HP_IRQHandler(void) WEAK;
void USB_LP_IRQHandler(void) WEAK;
void DMA1_Channel5_IRQHandler(void) WEAK;
void TIM2_IRQHandler(void) WEAK;
void TIM3_IRQHandler(void) WEAK;
void USART1_IRQHandler(void) WEAK;
void USART2_IRQHandler(void) WEAK;

__attribute__((section(".isr_vector"), used))
void (* const vector_table[16 + 43])(void) = {
    (void (*)(void))&_estack,
    Reset_Handler,
    NMI_Handler,
    HardFault_Handler,
    MemManage_Handler,
    BusFault_Handler,
    UsageFault_Handler,
    (void (*)(void))HL_IMAGE_MAGIC, 0, 0, 0,  /* reserved; marks hybridlink */
    SVC_Handler,
    DebugMon_Handler,
    0,
    PendSV_Handler,
    SysTick_Handler,
    [16 + 16] = DMA1_Channel6_IRQHandler,
    [16 + 17] = DMA1_Channel7_IRQHandler,
    [16 + 15] = DMA1_Channel5_IRQHandler,
    [16 + 19] = USB_HP_IRQHandler,
    [16 + 20] = USB_LP_IRQHandler,
    [16 + 28] = TIM2_IRQHandler,
    [16 + 29] = TIM3_IRQHandler,
    [16 + 37] = USART1_IRQHandler,
    [16 + 38] = USART2_IRQHandler,
};

void Reset_Handler(void)
{
    uint32_t *s = &_sidata, *d = &_sdata;
    *(volatile uint32_t *)0xE000ED08u = (uint32_t)vector_table;   /* VTOR */
    while (d < &_edata)
        *d++ = *s++;
    for (d = &_sbss; d < &_ebss; )
        *d++ = 0;
    main();
    for (;;) { }
}

void Default_Handler(void)
{
    for (;;) { }
}
