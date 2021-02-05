#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/pio.h"
#include "hardware/interp.h"

#include "mandelbrot.h"
#include "st7789_lcd.h"

#define USE_NUNCHUCK
#ifdef USE_NUNCHUCK
#include "nunchuck.h"
#endif

#define IMAGE_ROWS 320
#define IMAGE_COLS 320

#define DISPLAY_ROWS 240
#define DISPLAY_COLS 240

#define ZOOM_CENTRE_X -1.0023f
#define ZOOM_CENTRE_Y -0.3043f

#define ITERATION_FIXED_PT 22

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
#ifdef USE_NUNCHUCK
    nunchuck_init(12, 13);
#endif

    PIO pio = pio0;
    uint sm = 0;
    st7789_init(pio, sm);

    multicore_launch_core1(core1_entry);

    interp_config cfg = interp_default_config();
    interp_config_set_add_raw(&cfg, true);
    interp_config_set_shift(&cfg, ITERATION_FIXED_PT);
    interp_config_set_mask(&cfg, 0, 31 - ITERATION_FIXED_PT);
    interp_config_set_signed(&cfg, true);
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

    float zoomx = ZOOM_CENTRE_X;
    float zoomy = ZOOM_CENTRE_Y;
    const float zoomr = 0.8f * 0.5f;
    while (1) {
      fractal1.minx = zoomx - 1.55f;
      fractal1.maxx = zoomx + 1.55f;
      fractal1.miny = zoomy - 1.3f;
      fractal1.maxy = zoomy + 1.3f;
      float minx = fractal1.minx;
      float maxx = fractal1.maxx;
      float sizex = maxx - minx;
      float miny = fractal1.miny;
      float maxy = fractal1.maxy;
      float sizey = maxy - miny;
      uint32_t count_inside = IMAGE_ROWS * IMAGE_COLS;
      fractal1.count_inside = count_inside;
      multicore_fifo_push_blocking((uint32_t)&fractal1);
      multicore_fifo_pop_blocking();
      
      fractal_read = &fractal1;
      fractal_write = &fractal2;
      bool reset = false;

      while (!reset) {
        bool lastzoom = sizey < 0.0002f;
        if (!lastzoom) {
          fractal_write->minx = zoomx - zoomr * sizex;
          fractal_write->maxx = zoomx + zoomr * sizex;
          fractal_write->miny = zoomy - zoomr * sizey;
          fractal_write->maxy = zoomy + zoomr * sizey;
        } else {
          fractal_write->minx = minx;
          fractal_write->maxx = maxx;
          fractal_write->miny = miny;
          fractal_write->maxy = maxy;
        }
        fractal_write->count_inside = count_inside;

        printf("Generating in (%f, %f) - (%f, %f) Zoom centre: (%f, %f)\n",
              fractal_write->minx, fractal_write->miny,
              fractal_write->maxx, fractal_write->maxy,
              zoomx, zoomy);

        multicore_fifo_push_blocking((uint32_t)fractal_write);

        float zoomminx = zoomx - zoomr * (fractal_write->maxx - fractal_write->minx);
        float zoommaxx = zoomx + zoomr * (fractal_write->maxx - fractal_write->minx);
        float zoomminy = zoomy - zoomr * (fractal_write->maxy - fractal_write->miny);
        float zoommaxy = zoomy + zoomr * (fractal_write->maxy - fractal_write->miny);

        const float izoomr = 0.9975f * 0.5f;

        absolute_time_t start_time = get_absolute_time();
        for (int iz = 0; iz < 220; ++iz) {
          int imin = 0;
          int imax = DISPLAY_ROWS;
          int jmin = 0;
          int jmax = DISPLAY_COLS;
          if (minx < fractal_read->minx) jmin = 1 + (fractal_read->minx - minx) * DISPLAY_COLS / (maxx - minx);
          if (maxx > fractal_read->maxx) jmax = (fractal_read->maxx - minx) * DISPLAY_COLS / (maxx - minx);
          if (miny < fractal_read->miny) imin = 1 + (fractal_read->miny - miny) * DISPLAY_ROWS / (maxy - miny);
          if (maxy > fractal_read->maxy) imax = (fractal_read->maxy - miny) * DISPLAY_ROWS / (maxy - miny);

          count_inside = 0;
          int32_t y = (int32_t)(((miny - fractal_read->miny) / (fractal_read->maxy - fractal_read->miny)) * IMAGE_ROWS * (float)(1 << ITERATION_FIXED_PT));
          int32_t y_step = (int32_t)((sizey / ((fractal_read->maxy - fractal_read->miny) * DISPLAY_ROWS)) * IMAGE_ROWS * (float)(1 << ITERATION_FIXED_PT));
          int32_t x_start = (int32_t)(((minx - fractal_read->minx) / (fractal_read->maxx - fractal_read->minx)) * IMAGE_COLS * (float)(1 << ITERATION_FIXED_PT));
          interp0->base[0] = (int32_t)((sizex / ((fractal_read->maxx - fractal_read->minx) * DISPLAY_COLS)) * IMAGE_COLS * (float)(1 << ITERATION_FIXED_PT));

          // Offset x and y by half a step so that we get round to nearest
          y += y_step >> 1;
          x_start += interp0->base[0] >> 1;

          st7789_start_pixels(pio, sm);
          for (int i = 0; i < DISPLAY_ROWS; ++i, y += y_step) {

            if (i < imin || i >= imax) {
              for (int j = 0; j < DISPLAY_COLS; ++j) {
                st7789_lcd_put_pixel(pio, sm, 0);
              }
            }
            else {
              int image_i = y >> ITERATION_FIXED_PT;

              interp0->accum[0] = x_start;
              interp0->base[2] = (uintptr_t)(fractal_read->image + image_i * IMAGE_COLS);

              for (int j = 0; j < DISPLAY_COLS; ++j) {
                uint8_t* iter = (uint8_t*)interp0->pop[2];
                if (j < jmin || j >= jmax) {
                  st7789_lcd_put_pixel(pio, sm, 0);
                } else {
                  st7789_lcd_put_pixel(pio, sm, palette[*iter]);
                  if (*iter == 0) count_inside++;
                }
              }
            }
          }

#ifdef USE_NUNCHUCK
          reset = nunchuck_zbutton();

          int joyx = nunchuck_joyx();
          int joyy = nunchuck_joyy();

          if (abs(joyx) > 7) zoomx += (joyx >> 3) * sizex * 0.001f;
          if (abs(joyy) > 7) zoomy += (joyy >> 3) * sizey * 0.001f;

          if (zoomx > zoommaxx) zoomx = zoommaxx;
          if (zoomx < zoomminx) zoomx = zoomminx;
          if (zoomy > zoommaxy) zoomy = zoommaxy;
          if (zoomy < zoomminy) zoomy = zoomminy;
#endif

          if (lastzoom || reset) break;

          minx = zoomx - izoomr * sizex;
          maxx = zoomx + izoomr * sizex;
          miny = zoomy - izoomr * sizey;
          maxy = zoomy + izoomr * sizey;
          sizex = maxx - minx;
          sizey = maxy - miny;

          if (multicore_fifo_rvalid() &&
              minx >= fractal_write->minx &&
              maxx <= fractal_write->maxx &&
              miny >= fractal_write->miny &&
              maxy <= fractal_write->maxy)
          {
            break;
          }
        }
        absolute_time_t stop_time = get_absolute_time();
        printf("Frames in %lldus\n", absolute_time_diff_us(start_time, stop_time));

        multicore_fifo_pop_blocking();

        if (lastzoom && 
            minx >= fractal_read->minx &&
            maxx <= fractal_read->maxx &&
            miny >= fractal_read->miny &&
            maxy <= fractal_read->maxy)
        {
          break;
        }

        FractalBuffer* tmp = fractal_read;
        fractal_read = fractal_write;
        fractal_write = tmp;
      }

      if (!reset) {
#ifdef USE_NUNCHUCK
        for (int i = 0; i < 600; ++i) {
          sleep_ms(100);
          if (nunchuck_zbutton()) break;
        }
#else
        sleep_ms(2000);
#endif
      }
    }

    return 0;
}
