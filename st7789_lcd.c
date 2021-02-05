/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <math.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "hardware/dma.h"

#include "st7789_lcd.pio.h"

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 240

#define PIN_DIN 0
#define PIN_CLK 1
#define PIN_CS 2
#define PIN_DC 3
#define PIN_RESET 4
#define PIN_BL 5

#define SERIAL_CLK_DIV 1.f

// Format: cmd length (including cmd byte), post delay in units of 5 ms, then cmd payload
// Note the delays have been shortened a little
static const uint8_t st7789_init_seq[] = {
        1, 20, 0x01,                         // Software reset
        1, 10, 0x11,                         // Exit sleep mode
        2, 2, 0x3a, 0x55,                   // Set colour mode to 16 bit
        2, 0, 0x36, 0x00,                   // Set MADCTL: row then column, refresh is bottom to top ????
        5, 0, 0x2a, 0x00, 0x00, 0x00, 0xf0, // CASET: column addresses from 0 to 240 (f0)
        5, 0, 0x2b, 0x00, 0x00, 0x00, 0xf0, // RASET: row addresses from 0 to 240 (f0)
        1, 2, 0x21,                         // Inversion on, then 10 ms delay (supposedly a hack?)
        1, 2, 0x13,                         // Normal display on, then 10 ms delay
        1, 2, 0x29,                         // Main screen turn on, then wait 500 ms
        0                                     // Terminate list
};

static inline void lcd_set_dc_cs(bool dc, bool cs) {
    sleep_us(1);
    gpio_put_masked((1u << PIN_DC) | (1u << PIN_CS), !!dc << PIN_DC | !!cs << PIN_CS);
    sleep_us(1);
}

static inline void lcd_write_cmd(PIO pio, uint sm, const uint8_t *cmd, size_t count) {
    st7789_lcd_wait_idle(pio, sm);
    lcd_set_dc_cs(0, 0);
    st7789_lcd_put(pio, sm, *cmd++);
    if (count >= 2) {
        st7789_lcd_wait_idle(pio, sm);
        lcd_set_dc_cs(1, 0);
        for (size_t i = 0; i < count - 1; ++i)
            st7789_lcd_put(pio, sm, *cmd++);
    }
    st7789_lcd_wait_idle(pio, sm);
    lcd_set_dc_cs(1, 1);
}

void st7789_init(PIO pio, uint sm) {
    uint offset = pio_add_program(pio, &st7789_lcd_program);
    st7789_lcd_program_init(pio, sm, offset, PIN_DIN, PIN_CLK, SERIAL_CLK_DIV);

    gpio_init(PIN_CS);
    gpio_init(PIN_DC);
    gpio_init(PIN_RESET);
    gpio_init(PIN_BL);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_set_dir(PIN_DC, GPIO_OUT);
    gpio_set_dir(PIN_RESET, GPIO_OUT);
    gpio_set_dir(PIN_BL, GPIO_OUT);

    gpio_put(PIN_CS, 1);
    gpio_put(PIN_RESET, 1);

    const uint8_t *cmd = st7789_init_seq;
    while (*cmd) {
        lcd_write_cmd(pio, sm, cmd + 2, *cmd);
        sleep_ms(*(cmd + 1) * 5);
        cmd += *cmd + 2;
    }

    gpio_put(PIN_BL, 1);
}

void st7789_start_pixels(PIO pio, uint sm) {
    uint8_t cmd = 0x2c; // RAMWR
    st7789_lcd_wait_idle(pio, sm);
    st7789_set_pixel_mode(pio, sm, false);
    lcd_write_cmd(pio, sm, &cmd, 1);
    st7789_set_pixel_mode(pio, sm, true);
    lcd_set_dc_cs(1, 0);
}

void st7789_stop_pixels(PIO pio, uint sm) {
    st7789_lcd_wait_idle(pio, sm);
    lcd_set_dc_cs(1, 1);
    st7789_set_pixel_mode(pio, sm, false);
}

uint st7789_create_dma_channel(PIO pio, uint sm)
{
  uint chan = dma_claim_unused_channel(true);

  dma_channel_config c = dma_channel_get_default_config(chan);
  channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
  channel_config_set_dreq(&c, pio_get_dreq(pio, sm, true));
  channel_config_set_read_increment(&c, true);
  channel_config_set_write_increment(&c, false);

  dma_channel_configure(
        chan,          // Channel to be configured
        &c,            // The configuration we just created
        &pio->txf[sm], // The write address
        NULL,          // The initial read address - set later
        0,             // Number of transfers - set later
        false          // Don't start yet
    );

  return chan;
}

void st7789_dma_pixels(uint chan, const uint16_t* pixels, uint num_pixels)
{
  // Ensure any previous transfer is finished.
  dma_channel_wait_for_finish_blocking(chan);

  dma_channel_hw_addr(chan)->read_addr = (uintptr_t)pixels;
  dma_channel_hw_addr(chan)->transfer_count = num_pixels;
  uint ctrl = dma_channel_hw_addr(chan)->ctrl_trig;
  dma_channel_hw_addr(chan)->ctrl_trig = (ctrl | DMA_CH0_CTRL_TRIG_INCR_READ_BITS);
}

static uint32_t pixel_to_dma;

void st7789_dma_repeat_pixel(uint chan, uint16_t pixel, uint repeats)
{
  dma_channel_wait_for_finish_blocking(chan);

  pixel_to_dma = pixel;
  dma_channel_hw_addr(chan)->read_addr = (uintptr_t)&pixel_to_dma;
  dma_channel_hw_addr(chan)->transfer_count = repeats;
  uint ctrl = dma_channel_hw_addr(chan)->ctrl_trig;
  dma_channel_hw_addr(chan)->ctrl_trig = (ctrl & ~DMA_CH0_CTRL_TRIG_INCR_READ_BITS);
}
