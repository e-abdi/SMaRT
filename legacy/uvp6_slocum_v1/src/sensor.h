#pragma once

#define SENSOR_EXPECTED_STR   "INIT"
#define SENSOR_HW_CONF_STR    "HW_CONF"
#define SENSOR_TIMEOUT_MS     3000

// Baud to use for passthrough mode on both sides (glider <-> sensor)
#define PASSTHROUGH_BAUD 38400

// Start/Stop command formatting
#define SENSOR_START_PREFIX   "$start:ACQ_CSCS_002H"
#define SENSOR_START_NO_TIME  "$start:ACQ_CSCS_002H;"
#define SENSOR_STOP_CMD       "$stop;"

// Pick the EOL that your sensor expects
#define SENSOR_LINE_ENDING_LF   "\n"

#define SENSOR_RESTART_DELAY_MS 2000
#define SENSOR_STOP_DELAY_MS    200

// Status byte calculation: MSB = sensor, LSB = glider
// bit 1 (MSB): sensor status (1=HW_CONF seen, 0=not seen)
// bit 0 (LSB): glider status (1=HI/SD seen, 0=not seen)
#define STATUS_SENSOR_BIT   1
#define STATUS_GLIDER_BIT   0

