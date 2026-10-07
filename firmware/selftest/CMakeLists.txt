# SMaRT self-test firmware.
#
# A minimal, standalone diagnostic image for bringing up a bare SMaRT board
# one part at a time (see tools/assembly_check.py). It has no dependency on
# ../src: if this build breaks, the main firmware is not the cause.
#
#   west build -p -b rpi_pico firmware/selftest -d build-selftest
#   west flash -d build-selftest   (or drag-and-drop build-selftest/zephyr/zephyr.uf2)
#
cmake_minimum_required(VERSION 3.20.0)

# Reuse the board wiring (uart0/uart1 roles, relay GPIO) from the main app.
set(DTC_OVERLAY_FILE ${CMAKE_CURRENT_LIST_DIR}/../boards/rpi_pico.overlay CACHE STRING "" FORCE)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(smart_selftest)

target_sources(app PRIVATE src/main.c)
