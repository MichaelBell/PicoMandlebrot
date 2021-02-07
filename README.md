# Mandlebrot Set generator

Draws the Mandelbrot set to a 240x240 ST7789 display connected as in the pio/st7789 pico example.

The fractal slowly zooms in, with the drawing and interpolation handled by one core while the other core is generating the next zoomed image.

You can "drive" the zoom using a connected Wii Nunchuck, using I2C on pins 12 and 13.  In the unlikely event that you don't have a suitable Nunchuck, you can comment out the obvious line at the top of main.c and instead it will zoom into a random interesting location.
