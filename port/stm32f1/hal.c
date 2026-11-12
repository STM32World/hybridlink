/*
 * STM32F103 implementation of common/hal.h (register level, no vendor code).
 * SPDX-License-Identifier: MIT
 */
#include <string.h>
#include "stm32f1.h"
#include "hal.h"
#include "board.h"
#include "tusb.h"

#define SYSCLK_HZ   72000000u
#define PCLK1_HZ    36000000u
#define PCLK2_HZ    72000000u

#define DWT_CTRL    REG32(0xE0001000u)
#define DWT_CYCCNT  REG32(0xE0001004u)
#define DEMCR       REG32(0xE000EDFCu)

#define USART_SR_RXNE   (1u << 5)
#define USART_SR_TXE    (1u << 7)
#define USART_CR1_UE    (1u << 13)
#define USART_CR1_M     (1u << 12)
#define USART_CR1_PCE   (1u << 10)
#define USART_CR1_PS    (1u << 9)
#define USART_CR1_TXEIE (1u << 7)
#define USART_CR1_TE    (1u << 3)
#define USART_CR1_RE    (1u << 2)
#define USART_CR1_SBK   (1u << 0)
#define USART_CR3_DMAR  (1u << 6)

#define DMA_CCR_EN      (1u << 0)
#define DMA_CCR_CIRC    (1u << 5)
#define DMA_CCR_MINC    (1u << 7)

static volatile uint32_t ticks;

void SysTick_Handler(void)
{
    ticks++;
}

uint32_t hal_millis(void)
{
    return ticks;
}

/* TinyUSB OS-less tick */
uint32_t tusb_time_millis_api(void)
{
    return ticks;
}

/* ------------------------------------------------------------------ */
/* clocks                                                             */
/* ------------------------------------------------------------------ */

static void clock_to_hsi(void)
{
    RCC_CR |= RCC_CR_HSION;
    while (!(RCC_CR & RCC_CR_HSIRDY)) { }
    RCC_CFGR &= ~3u;
    while (RCC_CFGR & (3u << 2)) { }
    RCC_CR &= ~RCC_CR_PLLON;
    while (RCC_CR & RCC_CR_PLLRDY) { }
}

static void clock_init(void)
{
    clock_to_hsi();
    RCC_CR |= RCC_CR_HSEON;
    for (uint32_t t = 0; !(RCC_CR & RCC_CR_HSERDY); t++)
        if (t > 500000u)
            break;                          /* no crystal: USB unusable */
    FLASH_ACR = 0x12;                       /* prefetch, 2 wait states   */
    /* PLL = HSE * 9 = 72 MHz, APB1 /2, APB2 /1, ADC /6, USB = PLL / 1.5 */
    RCC_CFGR = (7u << 18) | (1u << 16) | (4u << 8) | (2u << 14);
    RCC_CR |= RCC_CR_PLLON;
    while (!(RCC_CR & RCC_CR_PLLRDY)) { }
    RCC_CFGR = (RCC_CFGR & ~3u) | 2u;
    while (((RCC_CFGR >> 2) & 3u) != 2u) { }

    DEMCR |= 1u << 24;
    DWT_CYCCNT = 0;
    DWT_CTRL |= 1u;
}

void hal_delay_us(uint32_t us)
{
    uint32_t start = DWT_CYCCNT, n = us * (SYSCLK_HZ / 1000000u);
    while ((DWT_CYCCNT - start) < n) { }
}

void hal_delay_ms(uint32_t ms)
{
    while (ms--)
        hal_delay_us(1000);
}

void hal_uid(uint32_t uid[3])
{
    for (int i = 0; i < 3; i++)
        uid[i] = REG32(UID_BASE + 4u * i);
}

void hal_watchdog_kick(void)
{
    IWDG_KR = 0xAAAA;
}

void hal_system_reset(void)
{
    nvic_system_reset();
    for (;;) { }
}

void hal_init(void)
{
    clock_init();
    SYST_RVR = SYSCLK_HZ / 1000u - 1u;
    SYST_CVR = 0;
    SYST_CSR = 7;

    RCC_APB2ENR |= RCC_APB2_AFIO | RCC_APB2_IOPA | RCC_APB2_IOPB | RCC_APB2_IOPC;
    /* SWD only: frees PA15, PB3 and PB4 */
    AFIO_MAPR = (AFIO_MAPR & ~(7u << 24)) | (2u << 24);

    hal_led(LED_IDLE);
}

void hal_jump(uint32_t vt)
{
    const uint32_t *v = (const uint32_t *)vt;

    __disable_irq();
    SYST_CSR = 0;
    for (int i = 0; i < 3; i++) {
        NVIC_ICER(i) = 0xFFFFFFFFu;
        NVIC_ICPR(i) = 0xFFFFFFFFu;
    }
    /* put peripherals back into reset state */
    RCC_APB1RSTR = 0xFFFFFFFFu; RCC_APB1RSTR = 0;
    RCC_APB2RSTR = 0xFFFFFFFFu; RCC_APB2RSTR = 0;
    RCC_APB1ENR = 0;
    RCC_APB2ENR = 0;
    RCC_AHBENR = 0x14;
    clock_to_hsi();
    RCC_CFGR = 0;
    RCC_CR &= ~(RCC_CR_HSEON | RCC_CR_HSEBYP);
    RCC_CIR = 0x009F0000u;
    FLASH_ACR = 0x30;

    SCB_VTOR = vt;
    __asm volatile (
        "msr msp, %0\n"
        "cpsie i\n"
        "bx %1\n" :: "r"(v[0]), "r"(v[1]) : "memory");
    for (;;) { }
}

/* ------------------------------------------------------------------ */
/* LED                                                                */
/* ------------------------------------------------------------------ */

void hal_led(enum hal_led s)
{
    switch (s) {
    case LED_IDLE:
        GPIO_BSRR(LED_PORT) = 1u << LED_PIN;
        gpio_mode(LED_PORT, LED_PIN, GPIO_OUT_PP_2);
        break;
    case LED_ACTIVE:
        GPIO_BRR(LED_PORT) = 1u << LED_PIN;
        gpio_mode(LED_PORT, LED_PIN, GPIO_OUT_PP_2);
        break;
    default:
        gpio_mode(LED_PORT, LED_PIN, GPIO_IN_FLOAT);
        break;
    }
}

/* ------------------------------------------------------------------ */
/* USB                                                                */
/* ------------------------------------------------------------------ */

void USB_HP_IRQHandler(void) { tud_int_handler(0); }
void USB_LP_IRQHandler(void) { tud_int_handler(0); }

void hal_usb_disconnect(void)
{
    GPIO_BRR(USB_RENUM_PORT) = 1u << USB_RENUM_PIN;
    gpio_mode(USB_RENUM_PORT, USB_RENUM_PIN, GPIO_OUT_OD_10);
    /* drive D+ low as well so the host notices immediately */
    GPIO_BRR(GPIOA_BASE) = 1u << 12;
    gpio_mode(GPIOA_BASE, 12, GPIO_OUT_PP_10);
}

void hal_usb_init(void)
{
    hal_usb_disconnect();
    hal_delay_ms(20);
    gpio_mode(GPIOA_BASE, 12, GPIO_IN_FLOAT);
    RCC_APB1ENR |= RCC_APB1_USB;
    RCC_APB1RSTR |= RCC_APB1_USB;
    RCC_APB1RSTR &= ~RCC_APB1_USB;
    /* release the pull-up: Q1 connects 1k5 to D+ */
    GPIO_BSRR(USB_RENUM_PORT) = 1u << USB_RENUM_PIN;
    NVIC_IPR(19) = 0x40;
    NVIC_IPR(20) = 0x40;
}

/* ------------------------------------------------------------------ */
/* internal flash                                                     */
/* ------------------------------------------------------------------ */

uint32_t hal_flash_size(void)
{
    uint32_t kb = FLASH_SIZE_KB;
    if (kb == 0 || kb == 0xFFFF || kb > 512)
        kb = 128;
    return kb * 1024u;
}

uint32_t hal_flash_page_size(void)
{
    return 1024u;
}

static void flash_unlock(void)
{
    if (FLASH_CR & FLASH_CR_LOCK) {
        FLASH_KEYR = 0x45670123u;
        FLASH_KEYR = 0xCDEF89ABu;
    }
}

static bool flash_wait(void)
{
    while (FLASH_SR & FLASH_SR_BSY) { }
    uint32_t sr = FLASH_SR;
    FLASH_SR = FLASH_SR_EOP | FLASH_SR_PGERR | FLASH_SR_WRPRTERR;
    return !(sr & (FLASH_SR_PGERR | FLASH_SR_WRPRTERR));
}

bool hal_flash_erase_page(uint32_t addr)
{
    flash_unlock();
    FLASH_CR = FLASH_CR_PER;
    FLASH_AR = addr;
    FLASH_CR = FLASH_CR_PER | FLASH_CR_STRT;
    bool ok = flash_wait();
    FLASH_CR = FLASH_CR_LOCK;
    const uint32_t *p = (const uint32_t *)addr;
    for (uint32_t i = 0; ok && i < hal_flash_page_size() / 4u; i++)
        ok = p[i] == 0xFFFFFFFFu;
    return ok;
}

bool hal_flash_write(uint32_t addr, const void *data, uint32_t len)
{
    const uint8_t *s = data;
    bool ok = true;
    flash_unlock();
    FLASH_CR = FLASH_CR_PG;
    for (uint32_t i = 0; ok && i < len; i += 2) {
        uint16_t hw = s[i] | ((i + 1 < len ? s[i + 1] : 0xFF) << 8);
        volatile uint16_t *dst = (volatile uint16_t *)(addr + i);
        if (*dst == hw)
            continue;
        *dst = hw;
        ok = flash_wait() && *dst == hw;
    }
    FLASH_CR = FLASH_CR_LOCK;
    return ok;
}

/* ------------------------------------------------------------------ */
/* target interface                                                   */
/* ------------------------------------------------------------------ */

static void adc_init(void)
{
    RCC_APB2ENR |= RCC_APB2_ADC1;
    gpio_mode(VTARGET_PORT, VTARGET_PIN, GPIO_IN_ANALOG);
    ADC1_CR2 = (1u << 23) | (7u << 17) | (1u << 20) | 1u;  /* TSVREFE, SWSTART trig, ADON */
    ADC1_SMPR1 = 7u << 21;                  /* ch17: 239.5 cycles */
    ADC1_SMPR2 = 7u << (3 * VTARGET_ADC_CH);
    hal_delay_us(20);
    ADC1_CR2 |= 1u << 3;                    /* RSTCAL */
    while (ADC1_CR2 & (1u << 3)) { }
    ADC1_CR2 |= 1u << 2;                    /* CAL */
    while (ADC1_CR2 & (1u << 2)) { }
}

static uint32_t adc_read(unsigned ch)
{
    uint32_t sum = 0;
    ADC1_SQR1 = 0;
    ADC1_SQR3 = ch;
    for (int i = 0; i < 8; i++) {
        ADC1_SR = 0;
        ADC1_CR2 |= 1u << 22;               /* SWSTART */
        while (!(ADC1_SR & 2u)) { }
        sum += ADC1_DR & 0xFFFu;
    }
    return sum / 8u;
}

void hal_target_voltage(uint32_t *vref, uint32_t *vtarget)
{
    *vref = adc_read(17);
    *vtarget = adc_read(VTARGET_ADC_CH);
}

void hal_target_nrst(bool assert)
{
    if (assert)
        GPIO_BRR(NRST_PORT) = 1u << NRST_PIN;
    else
        GPIO_BSRR(NRST_PORT) = 1u << NRST_PIN;
}

void hal_target_init(void)
{
    GPIO_BSRR(NRST_PORT) = 1u << NRST_PIN;
    gpio_mode(NRST_PORT, NRST_PIN, GPIO_OUT_OD_10);
    gpio_mode(SWCLK_ALT_PORT, SWCLK_ALT_PIN, GPIO_IN_FLOAT);
    gpio_mode(SWDIO_IN_PORT, SWDIO_IN_PIN, GPIO_IN_FLOAT);
    GPIO_BSRR(SWCLK_PORT) = 1u << SWCLK_PIN;
    gpio_mode(SWCLK_PORT, SWCLK_PIN, GPIO_OUT_PP_50);
    GPIO_BSRR(SWDIO_PORT) = 1u << SWDIO_PIN;
    gpio_mode(SWDIO_PORT, SWDIO_PIN, GPIO_OUT_PP_50);
    RCC_AHBENR |= RCC_AHB_DMA1;
    adc_init();
}

/* ------------------------------------------------------------------ */
/* VCP: USART2, RX by circular DMA (ch6), TX by interrupt ring         */
/* ------------------------------------------------------------------ */

#define VCP_RX_SIZE 512u
#define VCP_TX_SIZE 512u

static uint8_t vcp_rx_buf[VCP_RX_SIZE];
static uint32_t vcp_rx_tail;
static uint8_t vcp_tx_buf[VCP_TX_SIZE];
static volatile uint32_t vcp_tx_head, vcp_tx_tail;
static bool vcp_on;

void USART2_IRQHandler(void)
{
    if ((USART_SR(USART2_BASE) & USART_SR_TXE) && (USART_CR1(USART2_BASE) & USART_CR1_TXEIE)) {
        if (vcp_tx_tail != vcp_tx_head) {
            USART_DR(USART2_BASE) = vcp_tx_buf[vcp_tx_tail % VCP_TX_SIZE];
            vcp_tx_tail++;
        } else {
            USART_CR1(USART2_BASE) &= ~USART_CR1_TXEIE;
        }
    }
}

void hal_vcp_config(uint32_t baud, uint8_t bits, uint8_t parity, uint8_t stop)
{
    if (baud < 300 || baud > 4500000u)
        baud = 115200;
    if (!vcp_on) {
        RCC_APB1ENR |= RCC_APB1_USART2;
        RCC_AHBENR |= RCC_AHB_DMA1;
        gpio_mode(VCP_TX_PORT, VCP_TX_PIN, GPIO_AF_PP_50);
        GPIO_BSRR(VCP_RX_PORT) = 1u << VCP_RX_PIN;
        gpio_mode(VCP_RX_PORT, VCP_RX_PIN, GPIO_IN_PULL);
        vcp_on = true;
    }
    uint32_t clk = PCLK1_HZ;
    USART_CR1(USART2_BASE) = 0;
    DMA1_CCR(6) = 0;
    USART_BRR(USART2_BASE) = (clk + baud / 2u) / baud;
    uint32_t cr1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
    if (parity == 1 || parity == 2) {
        cr1 |= USART_CR1_PCE | (parity == 1 ? USART_CR1_PS : 0);
        if (bits == 8)
            cr1 |= USART_CR1_M;
    } else if (bits == 9) {
        cr1 |= USART_CR1_M;
    }
    USART_CR2(USART2_BASE) = (stop == 2) ? (2u << 12) : (stop == 1 ? (3u << 12) : 0);
    USART_CR3(USART2_BASE) = USART_CR3_DMAR;

    DMA1_CPAR(6) = USART2_BASE + 0x04;
    DMA1_CMAR(6) = (uint32_t)vcp_rx_buf;
    DMA1_CNDTR(6) = VCP_RX_SIZE;
    vcp_rx_tail = 0;
    DMA1_CCR(6) = DMA_CCR_MINC | DMA_CCR_CIRC | DMA_CCR_EN;
    USART_CR1(USART2_BASE) = cr1;
    NVIC_IPR(IRQ_USART2) = 0x80;
    nvic_enable(IRQ_USART2);
}

size_t hal_vcp_rx(uint8_t *buf, size_t max)
{
    if (!vcp_on)
        return 0;
    uint32_t head = VCP_RX_SIZE - DMA1_CNDTR(6);
    size_t n = 0;
    while (vcp_rx_tail != head && n < max) {
        buf[n++] = vcp_rx_buf[vcp_rx_tail];
        vcp_rx_tail = (vcp_rx_tail + 1u) % VCP_RX_SIZE;
    }
    return n;
}

size_t hal_vcp_tx_space(void)
{
    return VCP_TX_SIZE - (vcp_tx_head - vcp_tx_tail);
}

size_t hal_vcp_tx(const uint8_t *buf, size_t len)
{
    if (!vcp_on)
        return len;
    size_t n = 0;
    while (n < len && hal_vcp_tx_space()) {
        vcp_tx_buf[vcp_tx_head % VCP_TX_SIZE] = buf[n++];
        vcp_tx_head++;
    }
    USART_CR1(USART2_BASE) |= USART_CR1_TXEIE;
    return n;
}

void hal_vcp_set_break(bool on)
{
    if (on && vcp_on)
        USART_CR1(USART2_BASE) |= USART_CR1_SBK;
}

/* ------------------------------------------------------------------ */
/* SWO: USART1 RX (PA10) by circular DMA (ch5)                         */
/* ------------------------------------------------------------------ */

#define SWO_SIZE 4096u
static uint8_t swo_buf[SWO_SIZE];
static volatile uint32_t swo_wraps;     /* completed DMA laps            */
static uint32_t swo_tail;               /* bytes consumed, running count */
static uint32_t swo_dropped;
static bool swo_on;

void DMA1_Channel5_IRQHandler(void)
{
    uint32_t isr = DMA1_ISR;
    DMA1_IFCR = 0xFu << 16;             /* channel 5 flags */
    if (isr & (1u << 17))               /* TCIF5 */
        swo_wraps++;
}

/* bytes written by the DMA so far. A wrap whose interrupt is still pending
 * (the counter has already reloaded) is counted here, otherwise the head
 * would appear a whole buffer behind. */
static uint32_t swo_head(void)
{
    __disable_irq();
    uint32_t cnt = DMA1_CNDTR(5);
    uint32_t wraps = swo_wraps;
    if (DMA1_ISR & (1u << 17)) {        /* TCIF5 not yet handled */
        wraps++;
        cnt = DMA1_CNDTR(5);            /* certainly after the wrap now */
    }
    __enable_irq();
    return wraps * SWO_SIZE + (SWO_SIZE - cnt);
}

bool hal_swo_start(uint32_t baud)
{
    if (baud == 0 || baud > 4500000u)
        return false;
    RCC_APB2ENR |= RCC_APB2_USART1;
    RCC_AHBENR |= RCC_AHB_DMA1;
    gpio_mode(SWO_PORT, SWO_PIN, GPIO_IN_FLOAT);
    USART_CR1(USART1_BASE) = 0;
    DMA1_CCR(5) = 0;
    DMA1_IFCR = 0xFu << 16;
    uint32_t brr = (PCLK2_HZ + baud / 2u) / baud;
    if (brr < 16)
        brr = 16;
    USART_BRR(USART1_BASE) = brr;
    USART_CR2(USART1_BASE) = 0;
    USART_CR3(USART1_BASE) = USART_CR3_DMAR;
    DMA1_CPAR(5) = USART1_BASE + 0x04;
    DMA1_CMAR(5) = (uint32_t)swo_buf;
    DMA1_CNDTR(5) = SWO_SIZE;
    swo_wraps = 0;
    swo_tail = 0;
    swo_dropped = 0;
    DMA1_CCR(5) = DMA_CCR_MINC | DMA_CCR_CIRC | (1u << 1) /* TCIE */ | DMA_CCR_EN;
    NVIC_IPR(15) = 0x80;
    nvic_enable(15);
    USART_CR1(USART1_BASE) = USART_CR1_UE | USART_CR1_RE;
    swo_on = true;
    return true;
}

void hal_swo_stop(void)
{
    USART_CR1(USART1_BASE) = 0;
    DMA1_CCR(5) = 0;
    nvic_disable(15);
    swo_on = false;
}

size_t hal_swo_available(void)
{
    if (!swo_on)
        return 0;
    uint32_t n = swo_head() - swo_tail;
    if ((int32_t)n < 0)
        return 0;                       /* never step the tail backwards */
    if (n > SWO_SIZE - 256u) {
        /* overrun: keep the newest half of the buffer */
        uint32_t drop = n - SWO_SIZE / 2u;
        swo_tail += drop;
        swo_dropped += drop;
        n -= drop;
    }
    return n;
}

/* on, received, consumed, dropped, USART SR, BRR, CR1, CR3, DMA CCR,
 * CNDTR, GPIOA CRH, AFIO MAPR */
unsigned hal_swo_status(uint32_t *w, unsigned max)
{
    uint32_t v[] = { swo_on, swo_on ? swo_head() : 0, swo_tail, swo_dropped,
                     USART_SR(USART1_BASE), USART_BRR(USART1_BASE),
                     USART_CR1(USART1_BASE), USART_CR3(USART1_BASE),
                     DMA1_CCR(5), DMA1_CNDTR(5), GPIO_CRH(GPIOA_BASE), AFIO_MAPR };
    unsigned n = sizeof v / sizeof v[0];
    if (n > max)
        n = max;
    for (unsigned i = 0; i < n; i++)
        w[i] = v[i];
    return n;
}

size_t hal_swo_peek(const uint8_t **p)
{
    size_t n = hal_swo_available();
    uint32_t pos = swo_tail % SWO_SIZE;
    if (n > SWO_SIZE - pos)
        n = SWO_SIZE - pos;             /* contiguous part only */
    *p = swo_buf + pos;
    return n;
}

void hal_swo_consume(size_t n)
{
    swo_tail += n;
}
