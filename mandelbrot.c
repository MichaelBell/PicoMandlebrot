#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/interp.h"

// Cycle checking parameters
#define MAX_CYCLE_LEN 8          // Must be power of 2
#define MIN_CYCLE_CHECK_ITER 32  // Must be multiple of max cycle len
#define CYCLE_TOLERANCE (1<<18)

// Fixed point with 4 bits to the left of the point.
// Range [-8,8) with precision 2^-28
typedef int32_t fixed_pt_t;

static inline fixed_pt_t mul(fixed_pt_t a, fixed_pt_t b)
{
  int32_t ah = a >> 14;
  int32_t al = a & 0x3fff;
  int32_t bh = b >> 14;
  int32_t bl = b & 0x3fff;

  // Ignore al * bl as contribution to final result is tiny.
  fixed_pt_t r = ((ah * bl) + (al * bh)) >> 14;
  r += ah * bh;
  return r;
}

static inline fixed_pt_t square(fixed_pt_t a) {
  int32_t ah = a >> 14;
  int32_t al = a & 0x3fff;

  return ((ah * al) >> 13) + (ah * ah);
}

fixed_pt_t make_fixed(int32_t x) {
  return x << 28;
}

fixed_pt_t make_fixedf(float x) {
  return (int32_t)(x * 268435456.f);
}

uint16_t generate(uint8_t* buff, int16_t rows, int16_t cols,
                  uint16_t max_iter, uint16_t iter_offset, bool use_cycle_check,
                  fixed_pt_t minx, fixed_pt_t maxx,
                  fixed_pt_t miny, fixed_pt_t maxy)
{
  uint8_t* buffptr = buff;
  uint16_t min_iter = max_iter - 1;
  fixed_pt_t incx = (maxx - minx) / (cols - 1);
  fixed_pt_t incy = (maxy - miny) / (rows - 1);
  fixed_pt_t escape_square = make_fixed(4);

  fixed_pt_t oldx, oldy;
  uint16_t min_cycle_check_iter = use_cycle_check ? MIN_CYCLE_CHECK_ITER : 0xffff;

  for (int16_t i = 0; i < rows; ++i) {
    fixed_pt_t y0 = miny + incy * i;
    for (int16_t j = 0; j < cols; ++j) {
      fixed_pt_t x0 = minx + incx * j;

      fixed_pt_t x = x0;
      fixed_pt_t y = y0;

      uint16_t k = 1;
      for (; k < max_iter; ++k) {
        fixed_pt_t x_square = square(x);
        fixed_pt_t y_square = square(y);
        if (x_square + y_square > escape_square) break;

        if (k >= min_cycle_check_iter) {
          if ((k & (MAX_CYCLE_LEN - 1)) == 0) {
            oldx = x;
            oldy = y;
          }
          else
          {
            if (abs(x - oldx) < CYCLE_TOLERANCE && abs(y - oldy) < CYCLE_TOLERANCE) {
              // Found a cycle
              k = max_iter;
              break;
            }
          }
        }

        fixed_pt_t nextx = x_square - y_square + x0;
        y = 2 * mul(x,y) + y0;
        x = nextx;
      }
      if (k == max_iter) *buffptr++ = 0;
      else {
        if (k > iter_offset) k -= iter_offset;
        else k = 1;
        *buffptr++ = k;
        if (min_iter > k) min_iter = k;
      }
    }
  }
  return min_iter;
}
