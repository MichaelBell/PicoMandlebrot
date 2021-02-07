#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/pio.h"
#include "hardware/interp.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "hardware/regs/rosc.h"
#include "hardware/regs/addressmap.h"

#include "mandelbrot.h"
#include "st7789_lcd.h"

//#define USE_NUNCHUCK
#ifdef USE_NUNCHUCK
#include "nunchuck.h"
#endif

#define IMAGE_ROWS 340
#define IMAGE_COLS 340

#define DISPLAY_ROWS 240
#define DISPLAY_COLS 240

#define ZOOM_CENTRE_X -1.01f
#define ZOOM_CENTRE_Y -0.3125f
//#define ZOOM_CENTRE_X -1.0023f
//#define ZOOM_CENTRE_Y -0.3043f

#define ITERATION_FIXED_PT 22

uint8_t fractal_iter_buff[2][IMAGE_ROWS*IMAGE_COLS];
FractalBuffer fractal1, fractal2;

uint16_t pixel_row_buff[2][DISPLAY_COLS];

#define MAX_ITER 0xe0

void core1_entry() {
  mandel_init();

  while (true) {
    FractalBuffer* fractal = (void*)multicore_fifo_pop_blocking();

    absolute_time_t start_time = get_absolute_time();
    generate_fractal(fractal); 
    absolute_time_t stop_time = get_absolute_time();
    printf("Generated in %lldus core 0 did %d pixels\n", absolute_time_diff_us(start_time, stop_time), fractal->cols * (fractal->rows - fractal->iend) + fractal->cols - fractal->jend);

    multicore_fifo_push_blocking(1);
  }
}

void seed_random_from_rosc()
{
  uint32_t random = 0;
  uint32_t random_bit;
  volatile uint32_t *rnd_reg = (uint32_t *)(ROSC_BASE + ROSC_RANDOMBIT_OFFSET);

  for (int k = 0; k < 32; k++) {
    while (1) {
      random_bit = (*rnd_reg) & 1;
      if (random_bit != ((*rnd_reg) & 1)) break;
    }

    random = (random << 1) | random_bit;
  }

  srand(random);
} 

void choose_init_zoomc(FractalBuffer* f, float* zoomx, float* zoomy)
{
  // Choose a random location that has exactly 1 neighbour inside the set
  int chosen_i, chosen_j;
  int choices = 0;
  for (int i = 1; i < IMAGE_ROWS-1; ++i) {
    for (int j = 1; j < IMAGE_COLS-1; ++j) {
      if (f->buff[i * IMAGE_COLS + j] == 0) continue;
      
      int count = 0;
      if (f->buff[(i-1) * IMAGE_COLS + j] == 0) count++;
      if (f->buff[(i+1) * IMAGE_COLS + j] == 0) count++;
      if (f->buff[i * IMAGE_COLS + j-1] == 0) count++;
      if (f->buff[i * IMAGE_COLS + j+1] == 0) count++;
      
      if (count == 1) {
        if (rand() % ++choices == 0) {
          chosen_i = i;
          chosen_j = j;
        }
      }
    }
  }

  *zoomx = f->minx + chosen_j * (f->maxx - f->minx) / IMAGE_COLS;
  *zoomy = f->miny + chosen_i * (f->maxy - f->miny) / IMAGE_ROWS;
}

void refine_zoomc(FractalBuffer* f, float* zoomx, float* zoomy)
{
  // Choose a centre that has a boundary between green and red pixels
  // i.e. iteration number between 0x5f and 0x60
  
  int i = IMAGE_ROWS / 2;
  int j = IMAGE_COLS / 2;
  int dir = -1;
  int steps = 1;
  int steps_to_do = steps;

  while (steps < 24) {
    if (dir == 0) ++i;
    if (dir == 1) ++j;
    if (dir == 2) --i;
    if (dir == 3) --j;
    if (--steps_to_do == 0) {
      if (dir == 1 || dir == 3) steps++;
      if (++dir == 4) dir = 0;
      steps_to_do = steps;
    }

    if ((f->buff[i * IMAGE_COLS + j] & 0x7e) != 0x4e) continue;

    if (f->buff[(i-1) * IMAGE_COLS + j] >= 0x54 ||
        f->buff[(i+1) * IMAGE_COLS + j] >= 0x54 ||
        f->buff[i * IMAGE_COLS + j-1] >= 0x54 ||
        f->buff[i * IMAGE_COLS + j+1] >= 0x54) {
      *zoomx = f->minx + j * (f->maxx - f->minx) / IMAGE_COLS;
      *zoomy = f->miny + i * (f->maxy - f->miny) / IMAGE_ROWS;
      return;
    }
  }

  // Don't change the zoom if the criteria weren't met
}

int main()
{
    FractalBuffer* fractal_read;
    FractalBuffer* fractal_write;
    fractal1.buff = fractal_iter_buff[0];
    fractal1.rows = IMAGE_ROWS;
    fractal1.cols = IMAGE_COLS;
    fractal1.max_iter = MAX_ITER;
    fractal1.iter_offset = 0;
    fractal1.use_cycle_check = false;
    fractal2.buff = fractal_iter_buff[1];
    fractal2.rows = IMAGE_ROWS;
    fractal2.cols = IMAGE_COLS;
    fractal2.max_iter = MAX_ITER;
    fractal2.iter_offset = 0;
    fractal2.use_cycle_check = false;

    // Set clock speed to max in spec.
    // To overclock, you could try these settings:
    //   PLL 1500, 5, 2 => 150 MHZ
    //   PLL 1440, 3, 3 => 160 MHZ
    // Don't forget to change the peripheral clock setting below too.
    set_sys_clock_pll(1596 * MHZ, 6, 2);

    // Tell the periperal clock the new sys clock speed
    clock_configure(clk_peri,
                    0,
                    CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                    133 * MHZ,
                    133 * MHZ);

    stdio_init_all();
#ifdef USE_NUNCHUCK
    nunchuck_init(12, 13);
#endif

    seed_random_from_rosc();

    PIO pio = pio0;
    uint sm = 0;
    st7789_init(pio, sm);
    uint st7789_chan[2];
    st7789_create_dma_channels(pio, sm, st7789_chan);

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

#ifdef USE_NUNCHUCK
    float zoomx = ZOOM_CENTRE_X;
    float zoomy = ZOOM_CENTRE_Y;
#else
    float zoomx = -1.f;
    float zoomy = 0.f;
#endif
    const float zoomr = 0.85f * 0.5f;
    while (1) {
      fractal1.minx = zoomx - 1.75f;
      fractal1.maxx = zoomx + 1.75f;
      fractal1.miny = zoomy - 1.6f;
      fractal1.maxy = zoomy + 1.6f;
      float minx = fractal1.minx;
      float maxx = fractal1.maxx;
      float sizex = maxx - minx;
      float miny = fractal1.miny;
      float maxy = fractal1.maxy;
      float sizey = maxy - miny;
      fractal1.use_cycle_check = true;
      init_fractal(&fractal1);
      multicore_fifo_push_blocking((uint32_t)&fractal1);
      multicore_fifo_pop_blocking();
      
#ifndef USE_NUNCHUCK
      choose_init_zoomc(&fractal1, &zoomx, &zoomy);
#endif
      
      fractal2.count_inside = IMAGE_ROWS*IMAGE_COLS;
      fractal_read = &fractal1;
      fractal_write = &fractal2;
      bool reset = false;
      bool lastzoom = false;

      while (!reset) {
#ifdef USE_NUNCHUCK
        lastzoom |= sizey < 0.0002f;
#else
        lastzoom |= sizey < 0.0003f;
#endif
        float next_zoomx = zoomx;
        float next_zoomy = zoomy;
        if (!lastzoom) {
#ifndef USE_NUNCHUCK
          refine_zoomc(fractal_read, &next_zoomx, &next_zoomy);
#endif
          fractal_write->minx = next_zoomx - zoomr * sizex;
          fractal_write->maxx = next_zoomx + zoomr * sizex;
          fractal_write->miny = next_zoomy - zoomr * sizey;
          fractal_write->maxy = next_zoomy + zoomr * sizey;
        } else {
          fractal_write->minx = minx;
          fractal_write->maxx = maxx;
          fractal_write->miny = miny;
          fractal_write->maxy = maxy;
        }
        fractal_write->use_cycle_check = sizey > 0.01f && 
                    fractal_write->count_inside > (IMAGE_ROWS*IMAGE_COLS) / 16;

        printf("Generating in (%f, %f) - (%f, %f) Zoom centre: (%f, %f)\n",
              fractal_write->minx, fractal_write->miny,
              fractal_write->maxx, fractal_write->maxy,
              zoomx, zoomy);

        init_fractal(fractal_write);
        multicore_fifo_push_blocking((uint32_t)fractal_write);

        float zoomminx = zoomx - zoomr * (fractal_write->maxx - fractal_write->minx);
        float zoommaxx = zoomx + zoomr * (fractal_write->maxx - fractal_write->minx);
        float zoomminy = zoomy - zoomr * (fractal_write->maxy - fractal_write->miny);
        float zoommaxy = zoomy + zoomr * (fractal_write->maxy - fractal_write->miny);

        const float izoomr = 0.9955f * 0.5f;

        int iz = 1;
        absolute_time_t start_time = get_absolute_time();
        for (; iz < 140; ++iz) {
          int imin = 0;
          int imax = DISPLAY_ROWS;
          int jmin = 0;
          int jmax = DISPLAY_COLS;
          if (minx < fractal_read->minx) jmin = 1 + (fractal_read->minx - minx) * DISPLAY_COLS / (maxx - minx);
          if (maxx > fractal_read->maxx) jmax = (fractal_read->maxx - minx) * DISPLAY_COLS / (maxx - minx);
          if (miny < fractal_read->miny) imin = 1 + (fractal_read->miny - miny) * DISPLAY_ROWS / (maxy - miny);
          if (maxy > fractal_read->maxy) imax = (fractal_read->maxy - miny) * DISPLAY_ROWS / (maxy - miny);

          int32_t y = (int32_t)(((miny - fractal_read->miny) / (fractal_read->maxy - fractal_read->miny)) * IMAGE_ROWS * (float)(1 << ITERATION_FIXED_PT));
          int32_t y_step = (int32_t)((sizey / ((fractal_read->maxy - fractal_read->miny) * DISPLAY_ROWS)) * IMAGE_ROWS * (float)(1 << ITERATION_FIXED_PT));
          int32_t x_start = (int32_t)(((minx - fractal_read->minx) / (fractal_read->maxx - fractal_read->minx)) * IMAGE_COLS * (float)(1 << ITERATION_FIXED_PT));
          interp0->base[0] = (int32_t)((sizex / ((fractal_read->maxx - fractal_read->minx) * DISPLAY_COLS)) * IMAGE_COLS * (float)(1 << ITERATION_FIXED_PT));

          // Offset x and y by half a step so that we get round to nearest
          y += y_step >> 1;
          x_start += interp0->base[0] >> 1;

          st7789_start_pixels(pio, sm);
          for (int i = 0; i < DISPLAY_ROWS; ++i, y += y_step) {

            // This generates fractal until the DMA channel is ready again
            generate_steal(fractal_write, st7789_chan[i & 1]);

            if (i < imin || i >= imax) {
              st7789_dma_repeat_pixel(st7789_chan, i & 1, 0, DISPLAY_COLS);
            }
            else {
              int image_i = y >> ITERATION_FIXED_PT;

              interp0->accum[0] = x_start;
              interp0->base[2] = (uintptr_t)(fractal_read->buff + image_i * IMAGE_COLS);

              uint16_t* pixelptr = pixel_row_buff[i & 1];
              for (int j = 0; j < DISPLAY_COLS; ++j) {
                uint8_t* iter = (uint8_t*)interp0->pop[2];
                if (j < jmin || j >= jmax) {
                  *pixelptr++ = 0;
                } else {
                  *pixelptr++ = palette[*iter];
                }
              }
              st7789_dma_pixels(st7789_chan, i & 1, pixel_row_buff[i & 1], DISPLAY_COLS);
            }
          }

#ifdef USE_NUNCHUCK
          reset = nunchuck_zbutton();
          lastzoom |= nunchuck_cbutton();

          int joyx = nunchuck_joyx();
          int joyy = nunchuck_joyy();

          if (abs(joyx) > 7) zoomx += (joyx >> 3) * sizex * 0.001f;
          if (abs(joyy) > 7) zoomy += (joyy >> 3) * sizey * 0.001f;

          if (zoomx > zoommaxx) zoomx = zoommaxx;
          if (zoomx < zoomminx) zoomx = zoomminx;
          if (zoomy > zoommaxy) zoomy = zoommaxy;
          if (zoomy < zoomminy) zoomy = zoomminy;
#else
          if (zoomx < next_zoomx) zoomx = MIN(next_zoomx, zoomx + sizex * 0.0005f);
          if (zoomx > next_zoomx) zoomx = MAX(next_zoomx, zoomx - sizex * 0.0005f);
          if (zoomy < next_zoomy) zoomy = MIN(next_zoomy, zoomy + sizey * 0.0005f);
          if (zoomy > next_zoomy) zoomy = MAX(next_zoomy, zoomy - sizey * 0.0005f);
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
        uint32_t time_diff = absolute_time_diff_us(start_time, stop_time);
        printf("Frames in %dus (%d frames at %d FPS)\n", time_diff, iz, (iz * 1000000) / time_diff);

        if (!multicore_fifo_rvalid())
          generate_steal_until_done(fractal_write);
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

      st7789_stop_pixels(pio, sm);

      if (!reset) {
#ifdef USE_NUNCHUCK
        for (int i = 0; i < 600; ++i) {
          sleep_ms(100);
          if (nunchuck_zbutton()) break;
        }
#else
        sleep_ms(1000);
#endif
      }
    }

    return 0;
}
