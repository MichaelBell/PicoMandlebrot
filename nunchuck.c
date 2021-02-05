#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

// I2C defines
#define I2C_PORT i2c0
#define NUN_ADDR 0x52

static uint8_t nunchuck_buf[6];
static struct repeating_timer nunchuck_timer;

static int64_t nunchuck_read_data(alarm_id_t id, void* user_data)
{
  i2c_read_blocking(I2C_PORT, NUN_ADDR, nunchuck_buf, 6, false);
  return 0;
}

static bool nunchuck_req_data(struct repeating_timer* t) {
  const uint8_t req = 0;
  i2c_write_blocking(I2C_PORT, NUN_ADDR, &req, 1, false);

  add_alarm_in_us(375, nunchuck_read_data, NULL, true);
  return true;
}

bool nunchuck_init(uint i2c_sda_pin, uint i2c_scl_pin)
{
  i2c_init(I2C_PORT, 100*1000);

  gpio_set_function(i2c_sda_pin, GPIO_FUNC_I2C);
  gpio_set_function(i2c_scl_pin, GPIO_FUNC_I2C);
  gpio_pull_up(i2c_sda_pin);
  gpio_pull_up(i2c_scl_pin);

  const uint8_t cmd[] = { 0xF0, 0x55 };
  if (i2c_write_blocking(I2C_PORT, NUN_ADDR, cmd, 2, false) != 2) {
    printf("Init failed\n");
    return false;
  }
  sleep_ms(1);
  const uint8_t cmd2[] = { 0xFB, 0x00 };
  if (i2c_write_blocking(I2C_PORT, NUN_ADDR, cmd2, 2, false) != 2) {
    printf("Init failed\n");
    return false;
  }
  sleep_ms(1);

  add_repeating_timer_ms(20, nunchuck_req_data, NULL, &nunchuck_timer);

  return true;
}

int nunchuck_joyx() {
  return (int)nunchuck_buf[0] - 128;
}

int nunchuck_joyy() {
  return (int)nunchuck_buf[1] - 128;
}

bool nunchuck_zbutton() {
  return !(nunchuck_buf[5] & 1);
}

bool nunchuck_cbutton() {
  return !(nunchuck_buf[5] & 2);
}

