// This nunchuck library was put together using info Arduino nunchuck libraries:
// https://github.com/todbot/wiichuck_adapter/tree/master/firmware/WiichuckDemo
// and https://github.com/madhephaestus/WiiChuck
//

// Nunchuck uses I2C to communicate.  The library uses IC20 hardware
// resource so the pins specified must be ones the I2C0 can be mapped to
// and I2C0 can't be used by anything else at the same time.
//
// You can set which I2C resource is used at the top of nunchuck.c
bool nunchuck_init(uint i2c_sda_pin, uint i2c_scl_pin);

// Read current state, joystick values are in the range (-128,127)
int nunchuck_joyx();
int nunchuck_joyy();
bool nunchuck_zbutton();
bool nunchuck_cbutton();
