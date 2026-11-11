/*
 * Minimal STM32F103 register definitions used by hybridlink.
 * SPDX-License-Identifier: MIT
 */
#ifndef STM32F1_H
#define STM32F1_H

#include <stdint.h>

#define REG32(a) (*(volatile uint32_t *)(a))
#define REG16(a) (*(volatile uint16_t *)(a))

/* ---- RCC ---- */
#define RCC_BASE        0x40021000u
#define RCC_CR          REG32(RCC_BASE + 0x00)
#define RCC_CFGR        REG32(RCC_BASE + 0x04)
#define RCC_CIR         REG32(RCC_BASE + 0x08)
#define RCC_APB2RSTR    REG32(RCC_BASE + 0x0C)
#define RCC_APB1RSTR    REG32(RCC_BASE + 0x10)
#define RCC_AHBENR      REG32(RCC_BASE + 0x14)
#define RCC_APB2ENR     REG32(RCC_BASE + 0x18)
#define RCC_APB1ENR     REG32(RCC_BASE + 0x1C)
#define RCC_BDCR        REG32(RCC_BASE + 0x20)
#define RCC_CSR         REG32(RCC_BASE + 0x24)

#define RCC_CR_HSION    (1u << 0)
#define RCC_CR_HSIRDY   (1u << 1)
#define RCC_CR_HSEON    (1u << 16)
#define RCC_CR_HSERDY   (1u << 17)
#define RCC_CR_HSEBYP   (1u << 18)
#define RCC_CR_PLLON    (1u << 24)
#define RCC_CR_PLLRDY   (1u << 25)

#define RCC_CSR_RMVF    (1u << 24)
#define RCC_CSR_PINRSTF (1u << 26)
#define RCC_CSR_PORRSTF (1u << 27)
#define RCC_CSR_SFTRSTF (1u << 28)
#define RCC_CSR_IWDGRSTF (1u << 29)

#define RCC_APB2_AFIO   (1u << 0)
#define RCC_APB2_IOPA   (1u << 2)
#define RCC_APB2_IOPB   (1u << 3)
#define RCC_APB2_IOPC   (1u << 4)
#define RCC_APB2_ADC1   (1u << 9)
#define RCC_APB2_TIM1   (1u << 11)
#define RCC_APB2_SPI1   (1u << 12)
#define RCC_APB2_USART1 (1u << 14)
#define RCC_APB1_TIM2   (1u << 0)
#define RCC_APB1_TIM3   (1u << 1)
#define RCC_APB1_SPI2   (1u << 14)
#define RCC_APB1_USART2 (1u << 17)
#define RCC_APB1_USB    (1u << 23)
#define RCC_APB1_BKP    (1u << 27)
#define RCC_APB1_PWR    (1u << 28)
#define RCC_AHB_DMA1    (1u << 0)

/* ---- FLASH ---- */
#define FLASH_R_BASE    0x40022000u
#define FLASH_ACR       REG32(FLASH_R_BASE + 0x00)
#define FLASH_KEYR      REG32(FLASH_R_BASE + 0x04)
#define FLASH_OPTKEYR   REG32(FLASH_R_BASE + 0x08)
#define FLASH_SR        REG32(FLASH_R_BASE + 0x0C)
#define FLASH_CR        REG32(FLASH_R_BASE + 0x10)
#define FLASH_AR        REG32(FLASH_R_BASE + 0x14)
#define FLASH_OBR       REG32(FLASH_R_BASE + 0x1C)
#define FLASH_SR_BSY    (1u << 0)
#define FLASH_SR_PGERR  (1u << 2)
#define FLASH_SR_WRPRTERR (1u << 4)
#define FLASH_SR_EOP    (1u << 5)
#define FLASH_CR_PG     (1u << 0)
#define FLASH_CR_PER    (1u << 1)
#define FLASH_CR_STRT   (1u << 6)
#define FLASH_CR_LOCK   (1u << 7)

/* ---- System memory info ---- */
#define FLASH_SIZE_KB   REG16(0x1FFFF7E0u)
#define UID_BASE        0x1FFFF7E8u

/* ---- GPIO ---- */
#define GPIOA_BASE      0x40010800u
#define GPIOB_BASE      0x40010C00u
#define GPIOC_BASE      0x40011000u
#define GPIO_CRL(p)     REG32((p) + 0x00)
#define GPIO_CRH(p)     REG32((p) + 0x04)
#define GPIO_IDR(p)     REG32((p) + 0x08)
#define GPIO_ODR(p)     REG32((p) + 0x0C)
#define GPIO_BSRR(p)    REG32((p) + 0x10)
#define GPIO_BRR(p)     REG32((p) + 0x14)

/* pin mode nibbles (CNF:MODE) */
#define GPIO_IN_ANALOG      0x0
#define GPIO_IN_FLOAT       0x4
#define GPIO_IN_PULL        0x8
#define GPIO_OUT_PP_50      0x3
#define GPIO_OUT_PP_10      0x1
#define GPIO_OUT_PP_2       0x2
#define GPIO_OUT_OD_50      0x7
#define GPIO_OUT_OD_10      0x5
#define GPIO_AF_PP_50       0xB
#define GPIO_AF_OD_50       0xF

static inline void gpio_mode(uint32_t port, unsigned pin, uint32_t mode)
{
    volatile uint32_t *cr = (volatile uint32_t *)(port + (pin >= 8 ? 4 : 0));
    unsigned sh = (pin & 7) * 4;
    *cr = (*cr & ~(0xFu << sh)) | (mode << sh);
}

/* ---- AFIO ---- */
#define AFIO_BASE       0x40010000u
#define AFIO_MAPR       REG32(AFIO_BASE + 0x04)

/* ---- USB FS device ---- */
#define USB_BASE        0x40005C00u
#define USB_EPR(n)      REG32(USB_BASE + 4u * (n))
#define USB_CNTR        REG32(USB_BASE + 0x40)
#define USB_ISTR        REG32(USB_BASE + 0x44)
#define USB_FNR         REG32(USB_BASE + 0x48)
#define USB_DADDR       REG32(USB_BASE + 0x4C)
#define USB_BTABLE      REG32(USB_BASE + 0x50)
#define USB_PMA_BASE    0x40006000u

/* ---- SCB / NVIC / SysTick ---- */
#define SCB_CPUID       REG32(0xE000ED00u)
#define SCB_ICSR        REG32(0xE000ED04u)
#define SCB_VTOR        REG32(0xE000ED08u)
#define SCB_AIRCR       REG32(0xE000ED0Cu)
#define SYST_CSR        REG32(0xE000E010u)
#define SYST_RVR        REG32(0xE000E014u)
#define SYST_CVR        REG32(0xE000E018u)
#define NVIC_ISER(n)    REG32(0xE000E100u + 4u * (n))
#define NVIC_ICER(n)    REG32(0xE000E180u + 4u * (n))
#define NVIC_ICPR(n)    REG32(0xE000E280u + 4u * (n))
#define NVIC_IPR(n)     (*(volatile uint8_t *)(0xE000E400u + (n)))

#define IRQ_DMA1_CH6        16
#define IRQ_DMA1_CH7        17
#define IRQ_USB_LP          20
#define IRQ_USART1          37
#define IRQ_USART2          38

static inline void nvic_enable(unsigned irq)  { NVIC_ISER(irq >> 5) = 1u << (irq & 31); }
static inline void nvic_disable(unsigned irq) { NVIC_ICER(irq >> 5) = 1u << (irq & 31); }

/* ---- USART ---- */
#define USART1_BASE     0x40013800u
#define USART2_BASE     0x40004400u
#define USART_SR(b)     REG32((b) + 0x00)
#define USART_DR(b)     REG32((b) + 0x04)
#define USART_BRR(b)    REG32((b) + 0x08)
#define USART_CR1(b)    REG32((b) + 0x0C)
#define USART_CR2(b)    REG32((b) + 0x10)
#define USART_CR3(b)    REG32((b) + 0x14)

/* ---- DMA1 ---- */
#define DMA1_BASE       0x40020000u
#define DMA1_ISR        REG32(DMA1_BASE + 0x00)
#define DMA1_IFCR       REG32(DMA1_BASE + 0x04)
#define DMA1_CCR(c)     REG32(DMA1_BASE + 0x08 + 20u * ((c) - 1))
#define DMA1_CNDTR(c)   REG32(DMA1_BASE + 0x0C + 20u * ((c) - 1))
#define DMA1_CPAR(c)    REG32(DMA1_BASE + 0x10 + 20u * ((c) - 1))
#define DMA1_CMAR(c)    REG32(DMA1_BASE + 0x14 + 20u * ((c) - 1))

/* ---- ADC1 ---- */
#define ADC1_BASE       0x40012400u
#define ADC1_SR         REG32(ADC1_BASE + 0x00)
#define ADC1_CR1        REG32(ADC1_BASE + 0x04)
#define ADC1_CR2        REG32(ADC1_BASE + 0x08)
#define ADC1_SMPR1      REG32(ADC1_BASE + 0x0C)
#define ADC1_SMPR2      REG32(ADC1_BASE + 0x10)
#define ADC1_SQR1       REG32(ADC1_BASE + 0x2C)
#define ADC1_SQR3       REG32(ADC1_BASE + 0x34)
#define ADC1_DR         REG32(ADC1_BASE + 0x4C)

/* ---- IWDG ---- */
#define IWDG_KR         REG32(0x40003000u)

static inline void __disable_irq(void) { __asm volatile ("cpsid i" ::: "memory"); }
static inline void __enable_irq(void)  { __asm volatile ("cpsie i" ::: "memory"); }
static inline void __dsb(void) { __asm volatile ("dsb" ::: "memory"); }
static inline void __isb(void) { __asm volatile ("isb" ::: "memory"); }

static inline void nvic_system_reset(void)
{
    __dsb();
    SCB_AIRCR = (0x5FAu << 16) | (1u << 2);
    __dsb();
    for (;;) { }
}

#endif
