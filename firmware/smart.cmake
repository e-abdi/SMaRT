# Hardware-independent SMaRT sources. Shared by the application and the
# unit tests (tests/) so the same logic runs on the board and on native_sim.
set(SMART_SRC_DIR ${CMAKE_CURRENT_LIST_DIR}/src)

target_include_directories(app PRIVATE ${SMART_SRC_DIR})
target_sources(app PRIVATE
  ${SMART_SRC_DIR}/util/nmea.c
  ${SMART_SRC_DIR}/util/timeutil.c
  ${SMART_SRC_DIR}/util/template.c
  ${SMART_SRC_DIR}/util/fmt.c
  ${SMART_SRC_DIR}/core/linebuf.c
  ${SMART_SRC_DIR}/core/nav.c
  ${SMART_SRC_DIR}/core/events.c
  ${SMART_SRC_DIR}/core/app.c
  ${SMART_SRC_DIR}/core/controller.c
  ${SMART_SRC_DIR}/core/store.c
  ${SMART_SRC_DIR}/sensor/sensor.c
  ${SMART_SRC_DIR}/proc/proc.c
)
target_sources_ifdef(CONFIG_SMART_GLIDER_SLOCUM    app PRIVATE ${SMART_SRC_DIR}/glider/slocum_bsd.c)
target_sources_ifdef(CONFIG_SMART_GLIDER_SEAGLIDER app PRIVATE ${SMART_SRC_DIR}/glider/seaglider_logdev.c)
target_sources_ifdef(CONFIG_SMART_PROC_UVP6_LPM    app PRIVATE ${SMART_SRC_DIR}/proc/uvp6_lpm.c)
