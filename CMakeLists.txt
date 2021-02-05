cmake_minimum_required(VERSION 3.12)

include(pico_sdk_import.cmake)

project(breakout)

pico_sdk_init()

include_directories(../pimoroni-pico/libraries/pico_display/)
link_directories(../pimoroni-pico/build/libraries/pico_graphics/ ../pimoroni-pico/build/drivers/st7789/)

add_executable(
  breakout
  breakout.cpp
  ../pimoroni-pico/libraries/pico_display/pico_display.cpp
)

# Pull in pico libraries that we need
target_link_libraries(breakout pico_stdlib hardware_spi hardware_pwm hardware_dma pico_graphics st7789)

# create map/bin/hex file etc.
pico_add_extra_outputs(breakout)
