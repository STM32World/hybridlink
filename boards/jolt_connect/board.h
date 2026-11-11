/*
 * Jolt Connect (and the ST-Link/V2-1 found on early Nucleo boards)
 * pin assignment, STM32F103CB @ 8 MHz HSE.
 *
 *   PB13  SWCLK (output)          PA5 is tied to the same net, kept input
 *   PB14  SWDIO (output driver)   tri-stated while reading
 *   PB12  SWDIO (input sense)
 *   PB0   target NRST (open drain)
 *   PA9   bi-colour status LED (high / low / tri-state)
 *   PA15  USB D+ pull-up control (open drain, low = disconnected)
 *   PA2   VCP TX  (USART2)        PA3  VCP RX  (USART2)
 *   PA10  SWO     (USART1 RX)
 *   PA0   target VCC sense (1:2 divider), ADC channel 0
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef BOARD_H
#define BOARD_H

#define BOARD_NAME          "Jolt Connect"
#define BOARD_HSE_HZ        8000000u

#define SWCLK_PORT          GPIOB_BASE
#define SWCLK_PIN           13
#define SWDIO_PORT          GPIOB_BASE
#define SWDIO_PIN           14
#define SWDIO_IN_PORT       GPIOB_BASE
#define SWDIO_IN_PIN        12
#define SWCLK_ALT_PORT      GPIOA_BASE      /* second pin on the clock net */
#define SWCLK_ALT_PIN       5

#define NRST_PORT           GPIOB_BASE
#define NRST_PIN            0

#define LED_PORT            GPIOA_BASE
#define LED_PIN             9

#define USB_RENUM_PORT      GPIOA_BASE
#define USB_RENUM_PIN       15

#define VCP_TX_PORT         GPIOA_BASE
#define VCP_TX_PIN          2
#define VCP_RX_PORT         GPIOA_BASE
#define VCP_RX_PIN          3

#define SWO_PORT            GPIOA_BASE
#define SWO_PIN             10

#define VTARGET_PORT        GPIOA_BASE
#define VTARGET_PIN         0
#define VTARGET_ADC_CH      0

#endif
