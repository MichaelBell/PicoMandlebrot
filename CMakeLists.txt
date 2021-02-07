# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.12)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_BUILD_TYPE Release)

# initalize pico_sdk from installed location
# (note this can come from environment, CMake cache etc)
set(PICO_SDK_PATH "/home/pi/pico/pico-sdk")

# Pull in Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(mandelbrot C CXX)

# Initialise the Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(mandelbrot mandelbrot.c main.c st7789_lcd.c nunchuck.c)

pico_set_program_name(mandelbrot "mandelbrot")
pico_set_program_version(mandelbrot "0.1")

pico_enable_stdio_uart(mandelbrot 1)
pico_enable_stdio_usb(mandelbrot 0)

pico_generate_pio_header(mandelbrot ${CMAKE_CURRENT_LIST_DIR}/st7789_lcd.pio)

#set_source_files_properties(mandelbrot.c PROPERTIES COMPILE_FLAGS -O2)

# Add the standard library to the build
target_link_libraries(mandelbrot pico_stdlib)

# Add any user requested libraries
target_link_libraries(mandelbrot
        hardware_pio
        hardware_interp
        hardware_i2c
        hardware_dma
        pico_float pico_multicore
        )

pico_add_extra_outputs(mandelbrot)

