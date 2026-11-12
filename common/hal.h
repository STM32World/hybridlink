/*
 * Hardware abstraction used by the portable parts of hybridlink.
 *
 * Everything MCU specific lives behind this interface (port/<mcu>/) and the
 * board pin assignment (boards/<board>/board.h). The ST-Link protocol, the
 * SWD/ADIv5 engine, the DFU logic and the USB descriptors only use these
 * functions, plus the TinyUSB device stack.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef HAL_H
#define HAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- core / system -------------------------------------------------- */
void     hal_init(void);                /* clocks, tick, LED, USB pins     */
uint32_t hal_millis(void);
void     hal_delay_us(uint32_t us);
void     hal_delay_ms(uint32_t ms);
void     hal_uid(uint32_t uid[3]);      /* 96-bit unique ID                */
void     hal_system_reset(void) __attribute__((noreturn));
void     hal_jump(uint32_t vector_table) __attribute__((noreturn));
void     hal_watchdog_kick(void);

/* ---- USB ------------------------------------------------------------ */
void     hal_usb_init(void);            /* clock, re-enumeration, IRQs     */
void     hal_usb_disconnect(void);      /* force a bus disconnect          */

/* ---- LED ------------------------------------------------------------ */
enum hal_led { LED_OFF, LED_IDLE, LED_ACTIVE, LED_BOTH };
void     hal_led(enum hal_led state);

/* ---- internal flash (bootloader) ------------------------------------ */
uint32_t hal_flash_size(void);
uint32_t hal_flash_page_size(void);
bool     hal_flash_erase_page(uint32_t addr);
bool     hal_flash_write(uint32_t addr, const void *data, uint32_t len);

/* ---- target interface (application) --------------------------------- */
void     hal_target_init(void);         /* SWD pins idle, NRST released    */
void     hal_target_nrst(bool assert);
/* raw ADC readings: internal reference and target VCC divider            */
void     hal_target_voltage(uint32_t *vref_raw, uint32_t *vtarget_raw);

/* ---- virtual COM port UART ------------------------------------------ */
void     hal_vcp_config(uint32_t baud, uint8_t data_bits, uint8_t parity,
                        uint8_t stop_bits);   /* CDC line coding encoding */
size_t   hal_vcp_rx(uint8_t *buf, size_t max);      /* drain received bytes */
size_t   hal_vcp_tx_space(void);
size_t   hal_vcp_tx(const uint8_t *buf, size_t len);
void     hal_vcp_set_break(bool on);

/* ---- SWO (UART/NRZ trace) ------------------------------------------- */
bool     hal_swo_start(uint32_t baud);
void     hal_swo_stop(void);
size_t   hal_swo_available(void);
/* diagnostic words for the host tool (port specific), returns the count */
unsigned hal_swo_status(uint32_t *w, unsigned max);
/* contiguous received data without copying; consume what was used.
 * On overrun the oldest data is dropped. */
size_t   hal_swo_peek(const uint8_t **p);
void     hal_swo_consume(size_t n);

#endif
