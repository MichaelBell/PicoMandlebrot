void st7789_init(PIO pio, uint sm);
void st7789_start_pixels(PIO pio, uint sm);
void st7789_stop_pixels(PIO pio, uint sm);
void st7789_create_dma_channels(PIO pio, uint sm, uint chan[2]);
void st7789_dma_pixels(uint chan[2], uint chan_idx, const uint16_t* pixels, uint num_pixels);
void st7789_dma_repeat_pixel(uint chan[2], uint chan_idx, uint16_t pixel, uint repeats);

#include "st7789_lcd.pio.h"
