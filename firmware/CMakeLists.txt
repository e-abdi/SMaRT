# SMaRT firmware
#
# Build with a profile (a folder under profiles/ holding smart.conf):
#   west build -b rpi_pico firmware -- -DSMART_PROFILE=uvp6_slocum
#
cmake_minimum_required(VERSION 3.20.0)

if(NOT DEFINED SMART_PROFILE)
  set(SMART_PROFILE uvp6_slocum)
endif()
set(SMART_PROFILE_DIR ${CMAKE_CURRENT_LIST_DIR}/profiles/${SMART_PROFILE})
if(NOT EXISTS ${SMART_PROFILE_DIR}/smart.conf)
  message(FATAL_ERROR "SMaRT profile '${SMART_PROFILE}' not found (expected ${SMART_PROFILE_DIR}/smart.conf)")
endif()
list(APPEND EXTRA_CONF_FILE ${SMART_PROFILE_DIR}/smart.conf)
message(STATUS "SMaRT profile: ${SMART_PROFILE}")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(smart)

include(${CMAKE_CURRENT_LIST_DIR}/smart.cmake)
target_sources(app PRIVATE
  src/main.c
  src/hw/port_uart.c
  src/hw/board_io.c
)
