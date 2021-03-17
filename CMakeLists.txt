cmake_minimum_required(VERSION 3.12)

include(pico_sdk_import.cmake)
include(pico_extras_import.cmake)

project(breakout-vga)

pico_sdk_init()

add_executable(
  breakout-vga
  breakout.cpp
)

add_compile_definitions(breakout-vga PRIVATE
  PICO_SCANVIDEO_PLANE_COUNT=3
  PICO_SCANVIDEO_MAX_SCANLINE_BUFFER2_WORDS=10
  PICO_SCANVIDEO_MAX_SCANLINE_BUFFER3_WORDS=10
  )

# Pull in pico libraries that we need
target_link_libraries(breakout-vga
  pico_stdlib
  pico_scanvideo_dpi
  hardware_pwm
  )

# create map/bin/hex file etc.
pico_add_extra_outputs(breakout-vga)
