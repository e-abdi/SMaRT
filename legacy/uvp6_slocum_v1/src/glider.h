#pragma once

// Startup tokens from Slocum (Backseat Driver)
#define GLIDER_EXPECTED_STR   "$HI"
#define GLIDER_SD_EXPECTED    "$SD"
#define GLIDER_TIMEOUT_MS     3000

// $SD message field indices (NEW: 4=epoch, 5=depth, 6=mode)
#define GLIDER_SD_EPOCH_FIELD  4   // SD field 4: epoch time
#define GLIDER_SD_DEPTH_FIELD  5   // SD field 5: depth
#define GLIDER_SD_MODE_FIELD   6   // SD field 6: mode

enum glider_mode {
    GLIDER_MODE_UNKNOWN = -1,
    GLIDER_MODE_DIVING  = 1,
    GLIDER_MODE_CLIMBING= 2,
    GLIDER_MODE_HOVER   = 3
};

// Power-up phase timeout
#define POWERUP_TIMEOUT_MS     2000

