# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.12)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# initalize pico_sdk from installed location
# (note this can come from environment, CMake cache etc)
set(PICO_SDK_PATH "/home/pi/pico/pico-sdk")

# Pull in Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(mandelbrot C CXX)

# Initialise the Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(mandelbrot mandelbrot.c )

pico_set_program_name(mandelbrot "mandelbrot")
pico_set_program_version(mandelbrot "0.1")

pico_enable_stdio_uart(mandelbrot 0)
pico_enable_stdio_usb(mandelbrot 1)

# Add the standard library to the build
target_link_libraries(mandelbrot pico_stdlib)

# Add any user requested libraries
target_link_libraries(mandelbrot
        hardware_pio
        hardware_interp
        pico_float pico_int64_ops
        )

pico_add_extra_outputs(mandelbrot)

