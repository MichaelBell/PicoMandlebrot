#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/interp.h"

#define IMAGE_ROWS 33
#define IMAGE_COLS 80

uint16_t fractal_image[IMAGE_ROWS*IMAGE_COLS];

// Fixed point with 4 bits to the left of the point.
// Range [-16,16) with precision 2^-27
typedef int32_t fixed_pt_t;

fixed_pt_t mul(fixed_pt_t a, fixed_pt_t b)
{
  // Later could add assembly optimized version, depending on how
  // good gcc's output it.
  int64_t res64 = ((int64_t)a) * ((int64_t)b);
  return res64 >> 27;
}

fixed_pt_t make_fixed(int32_t x) {
  return x << 27;
}

fixed_pt_t make_fixedf(float x) {
  return (int32_t)(x * 134217728.f);
}

uint16_t generate(uint16_t* buff, int16_t rows, int16_t cols, uint16_t max_iter,
                  fixed_pt_t minx, fixed_pt_t maxx,
                  fixed_pt_t miny, fixed_pt_t maxy)
{
  uint16_t min_iter = max_iter - 1;
  fixed_pt_t incx = (maxx - minx) / (cols - 1);
  fixed_pt_t incy = (maxy - miny) / (rows - 1);
  fixed_pt_t escape_square = make_fixed(4);

  for (int16_t i = 0; i < rows; ++i) {
    fixed_pt_t y0 = miny + incy * i;
    for (int16_t j = 0; j < cols; ++j) {
      fixed_pt_t x0 = minx + incx * j;

      fixed_pt_t x = x0;
      fixed_pt_t y = y0;

      uint16_t k = 1;
      for (; k < max_iter; ++k) {
        fixed_pt_t x_square = mul(x,x);
        fixed_pt_t y_square = mul(y,y);
        if (x_square + y_square > escape_square) break;

        fixed_pt_t nextx = x_square - y_square + x0;
        y = 2 * mul(x,y) + y0;
        x = nextx;
      }
      if (k == max_iter) buff[i*cols + j] = 0;
      else {
        buff[i*cols + j] = k;
        if (min_iter > k) min_iter = k;
      }
    }
  }
  return min_iter;
}

#define MAX_ITER 128

int main()
{
    stdio_init_all();

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
      float zoomr = 0.9f;
      uint16_t min_iter = 0;

      for (int z = 0; z < 50; ++z) {
        absolute_time_t start_time = get_absolute_time();
        min_iter = 
          generate(fractal_image, IMAGE_ROWS, IMAGE_COLS, MAX_ITER + min_iter,
                   make_fixedf(minx), make_fixedf(maxx),
                   make_fixedf(miny), make_fixedf(maxy));
        min_iter--;
        absolute_time_t stop_time = get_absolute_time();

        puts("\x1b[2J\x1b[H");
        char buff[6*IMAGE_COLS+2];
        char* buffptr;
        for (int i = 0; i < IMAGE_ROWS; ++i) {
          buffptr = buff;
          for (int j = 0; j < IMAGE_COLS; ++j) {
            uint16_t iter = fractal_image[i*IMAGE_COLS + j];
            if (iter == 0) *buffptr++ = ' ';
            else {
              *buffptr++ = 0x1b;
              *buffptr++ = '[';
              *buffptr++ = '3';
              *buffptr++ = '1' + palette[iter - min_iter];
              *buffptr++ = 'm';
              *buffptr++ = 'X';
            }
          }
          *buffptr++ = 0;
          puts(buff);
        }

        printf("\x1b[37mGenerated in %lldus\n", absolute_time_diff_us(start_time, stop_time));

        minx = minx * zoomr + zoomx * (1.f - zoomr);
        maxx = maxx * zoomr + zoomx * (1.f - zoomr);
        miny = miny * zoomr + zoomy * (1.f - zoomr);
        maxy = maxy * zoomr + zoomy * (1.f - zoomr);

        sleep_ms(100);
      }

      sleep_ms(2000);
    }

    return 0;
}
