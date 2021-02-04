#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/interp.h"

#include "mandelbrot.h"

#define IMAGE_ROWS 66
#define IMAGE_COLS 160

#define DISPLAY_ROWS 33
#define DISPLAY_COLS 80

uint8_t fractal_image[IMAGE_ROWS*IMAGE_COLS];

#define MAX_ITER 128

int main()
{
    stdio_init_all();
    interp_config cfg = interp_default_config();
    interp_config_set_add_raw(&cfg, true);
    interp_config_set_shift(&cfg, 23);
    interp_config_set_mask(&cfg, 0, 8);
    interp_set_config(interp0, 0, &cfg);
    interp0->base[1] = 0;
    interp0->accum[1] = 0;

    int8_t palette[MAX_ITER] = { 0,1,2,3,4,5,6 };
    for (int i = 7, c = 0, k = 2; i < MAX_ITER; k <<= 1) {
      for (int j = 0; j < k && i < MAX_ITER; ++i,++j) {
        palette[i] = c;
      }
      if (++c > 6) c = 0;
    }

    while (1) {
      float minx = -2.1f;
      float maxx = 1.2f;
      float miny = -1.3f;
      float maxy = 1.3f;
      float zoomx = -1.f;
      float zoomy = -0.305f;
      float zoomr = 0.5f;
      uint16_t min_iter = 0;
      uint32_t count_inside = IMAGE_ROWS * IMAGE_COLS;

      for (int z = 0; z < 10; ++z) {

        absolute_time_t start_time = get_absolute_time();
        min_iter = 
          generate(fractal_image, IMAGE_ROWS, IMAGE_COLS, 
                   MAX_ITER /* + min_iter*/,
                   0,
                   count_inside > (IMAGE_ROWS * IMAGE_COLS) / 16,
                   make_fixedf(minx), make_fixedf(maxx),
                   make_fixedf(miny), make_fixedf(maxy));
        min_iter--;
        absolute_time_t stop_time = get_absolute_time();

        float iminx = minx;
        float imaxx = maxx;
        float iminy = miny;
        float imaxy = maxy;
        float izoomr = 0.8909f;
        for (int iz = 0; iz < 5; ++iz) {
          puts("\x1b[2J\x1b[H");

          char buff[6*DISPLAY_COLS+2];
          char* buffptr;
          count_inside = 0;
          uint32_t y = (uint32_t)(((iminy - miny) / (maxy - miny)) * IMAGE_ROWS * (float)(1 << 23));
          uint32_t y_step = (uint32_t)(((imaxy - iminy) / ((maxy - miny) * DISPLAY_ROWS)) * IMAGE_ROWS * (float)(1 << 23));
          uint32_t x_start = (uint32_t)(((iminx - minx) / (maxx - minx)) * IMAGE_COLS * (float)(1 << 23));
          interp0->base[0] = (uint32_t)(((imaxx - iminx) / ((maxx - minx) * DISPLAY_COLS)) * IMAGE_COLS * (float)(1 << 23));

          for (int i = 0; i < DISPLAY_ROWS; ++i, y += y_step) {
            buffptr = buff;
            int image_i = y >> 23;

            interp0->accum[0] = x_start;
            interp0->base[2] = (uintptr_t)(fractal_image + image_i * IMAGE_COLS);

            for (int j = 0; j < DISPLAY_COLS; ++j) {
              uint8_t iter = *(uint8_t*)interp0->pop[2];
              if (iter == 0) {
                *buffptr++ = ' ';
                count_inside++;
              } else {
                *buffptr++ = 0x1b;
                *buffptr++ = '[';
                *buffptr++ = '3';
                *buffptr++ = '1' + palette[iter /*- min_iter*/];
                *buffptr++ = 'm';
                *buffptr++ = 'X';
              }
            }
            *buffptr++ = 0;
            puts(buff);
          }

          printf("\x1b[37mGenerated in %lldus\n", absolute_time_diff_us(start_time, stop_time));

          iminx = iminx * izoomr + zoomx * (1.f - izoomr);
          imaxx = imaxx * izoomr + zoomx * (1.f - izoomr);
          iminy = iminy * izoomr + zoomy * (1.f - izoomr);
          imaxy = imaxy * izoomr + zoomy * (1.f - izoomr);
          
          if (iz != 4) sleep_ms(200);
          sleep_ms(200);
        }

        minx = minx * zoomr + zoomx * (1.f - zoomr);
        maxx = maxx * zoomr + zoomx * (1.f - zoomr);
        miny = miny * zoomr + zoomy * (1.f - zoomr);
        maxy = maxy * zoomr + zoomy * (1.f - zoomr);

        //sleep_ms(100);
      }

      sleep_ms(2000);
    }

    return 0;
}
