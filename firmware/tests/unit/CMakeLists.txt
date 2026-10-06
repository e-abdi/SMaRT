# Unit and scenario tests on the host (native_sim), built against a real
# profile so the shipped configuration is what gets tested:
#
#   west build -b native_sim/native/64 firmware/tests/unit -- -DSMART_PROFILE=uvp6_slocum
#   build/zephyr/zephyr.exe
#
cmake_minimum_required(VERSION 3.20.0)

set(SMART_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
if(NOT DEFINED SMART_PROFILE)
  set(SMART_PROFILE uvp6_slocum)
endif()
list(APPEND EXTRA_CONF_FILE ${SMART_ROOT}/profiles/${SMART_PROFILE}/smart.conf)
set(KCONFIG_ROOT ${SMART_ROOT}/Kconfig)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(smart_unit)

include(${SMART_ROOT}/smart.cmake)
target_sources(app PRIVATE
  src/fake_hw.c
  src/test_util.c
  src/test_scenario_slocum.c
  src/test_scenario_seaglider.c
)
