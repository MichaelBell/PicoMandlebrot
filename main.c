#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/pio.h"
#include "hardware/interp.h"

#include "mandelbrot.h"
#include "st7789_lcd.h"

#define IMAGE_ROWS 320
#define IMAGE_COLS 320

#define DISPLAY_ROWS 240
#define DISPLAY_COLS 240

typedef struct {
  uint8_t image[IMAGE_ROWS*IMAGE_COLS];

  uint32_t count_inside;
  uint32_t min_iter;
  float minx, miny, maxx, maxy;
} FractalBuffer;

FractalBuffer fractal1, fractal2;

#define MAX_ITER 0xe0

void core1_entry() {
  mandel_init();

  while (true) {
    FractalBuffer* fractal = (void*)multicore_fifo_pop_blocking();

    absolute_time_t start_time = get_absolute_time();
    fractal->min_iter = 
          generate(fractal->image, IMAGE_ROWS, IMAGE_COLS, 
                   MAX_ITER /* + min_iter*/,
                   0,
                   fractal->count_inside > (IMAGE_ROWS * IMAGE_COLS) / 16,
                   make_fixedf(fractal->minx), make_fixedf(fractal->maxx),
                   make_fixedf(fractal->miny), make_fixedf(fractal->maxy));
    fractal->min_iter--;
    absolute_time_t stop_time = get_absolute_time();
    printf("Generated in %lldus\n", absolute_time_diff_us(start_time, stop_time));

    multicore_fifo_push_blocking(1);
  }
}

int main()
{
    FractalBuffer* fractal_read;
    FractalBuffer* fractal_write;

    stdio_init_all();

    PIO pio = pio0;
    uint sm = 0;
    st7789_init(pio, sm);

    multicore_launch_core1(core1_entry);

    interp_config cfg = interp_default_config();
    interp_config_set_add_raw(&cfg, true);
    interp_config_set_shift(&cfg, 23);
    interp_config_set_mask(&cfg, 0, 8);
    interp_set_config(interp0, 0, &cfg);
    interp0->base[1] = 0;
    interp0->accum[1] = 0;

    uint16_t palette[MAX_ITER];
    for (int i = 0; i < MAX_ITER; ++i) {
      //palette[i] = ((i & 7) << 2) | ((i & 0x18) << 5) | ((i & 0xe0) << 8);
      //
      //if (i < 0x40)
      //  palette[i] = ((i & 3) << 3) | ((i & 0xc) << 6) | ((i & 0x30) << 10);
      //else
      //  palette[i] = ((i & 6) << 2) | ((i & 0x18) << 5) | ((i & 0x70) << 9);
      
      if (i < 0x20) palette[i] = i;
      else if (i < 0x60) palette[i] = (i - 0x20) << 5;
      else if (i < 0xc0) palette[i] = ((i - 0x60) >> 2) << 11;
      else palette[i] = (i - 0xc0) >> 3;
    }

    while (1) {
      fractal1.minx = -2.1f;
      fractal1.maxx = 1.2f;
      fractal1.miny = -1.3f;
      fractal1.maxy = 1.3f;
      float zoomx = -1.0023f;
      float zoomy = -0.3043f;
      float zoomr = 0.5f;
      uint32_t count_inside = IMAGE_ROWS * IMAGE_COLS;
      fractal1.count_inside = count_inside;
      multicore_fifo_push_blocking((uint32_t)&fractal1);
      multicore_fifo_pop_blocking();
      
      fractal_read = &fractal1;
      fractal_write = &fractal2;
      const int MAXZ = 10;

      for (int z = 0; z < MAXZ + 1; ++z) {
        if (z != MAXZ) {
          fractal_write->minx = fractal_read->minx * zoomr + zoomx * (1.f - zoomr);
          fractal_write->maxx = fractal_read->maxx * zoomr + zoomx * (1.f - zoomr);
          fractal_write->miny = fractal_read->miny * zoomr + zoomy * (1.f - zoomr);
          fractal_write->maxy = fractal_read->maxy * zoomr + zoomy * (1.f - zoomr);
          fractal_write->count_inside = count_inside;

          multicore_fifo_push_blocking((uint32_t)fractal_write);
        }

        float iminx = fractal_read->minx;
        float imaxx = fractal_read->maxx;
        float iminy = fractal_read->miny;
        float imaxy = fractal_read->maxy;
        float izoomr = 0.99655744578877920100867279701475f;

        absolute_time_t start_time = get_absolute_time();
        for (int iz = 0; iz < 200; ++iz) {

          count_inside = 0;
          uint32_t y = (uint32_t)(((iminy - fractal_read->miny) / (fractal_read->maxy - fractal_read->miny)) * IMAGE_ROWS * (float)(1 << 23));
          uint32_t y_step = (uint32_t)(((imaxy - iminy) / ((fractal_read->maxy - fractal_read->miny) * DISPLAY_ROWS)) * IMAGE_ROWS * (float)(1 << 23));
          uint32_t x_start = (uint32_t)(((iminx - fractal_read->minx) / (fractal_read->maxx - fractal_read->minx)) * IMAGE_COLS * (float)(1 << 23));
          interp0->base[0] = (uint32_t)(((imaxx - iminx) / ((fractal_read->maxx - fractal_read->minx) * DISPLAY_COLS)) * IMAGE_COLS * (float)(1 << 23));

          // Offset x and y by half a step so that we get round to nearest
          y += y_step >> 1;
          x_start += interp0->base[0] >> 1;

          st7789_start_pixels(pio, sm);
          for (int i = 0; i < DISPLAY_ROWS; ++i, y += y_step) {
            int image_i = y >> 23;

            interp0->accum[0] = x_start;
            interp0->base[2] = (uintptr_t)(fractal_read->image + image_i * IMAGE_COLS);

            for (int j = 0; j < DISPLAY_COLS; ++j) {
              uint8_t iter = *(uint8_t*)interp0->pop[2];
              st7789_lcd_put_pixel(pio, sm, palette[iter]);
              if (iter == 0) count_inside++;
            }
          }

          if (z == MAXZ) break;

          iminx = iminx * izoomr + zoomx * (1.f - izoomr);
          imaxx = imaxx * izoomr + zoomx * (1.f - izoomr);
          iminy = iminy * izoomr + zoomy * (1.f - izoomr);
          imaxy = imaxy * izoomr + zoomy * (1.f - izoomr);
        }
        absolute_time_t stop_time = get_absolute_time();
        printf("Frames in %lldus\n", absolute_time_diff_us(start_time, stop_time));

        FractalBuffer* tmp = fractal_read;
        fractal_read = fractal_write;
        fractal_write = tmp;

        if (z != MAXZ) multicore_fifo_pop_blocking();
      }

      sleep_ms(2000);
    }

    return 0;
}
