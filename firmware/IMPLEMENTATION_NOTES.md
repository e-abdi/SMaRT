# Implementation Notes - Multi-Sensor Integration with New State Machine

## Major Changes

### 1. State Machine Expansion (4 States)
- **STATE_POWERUP**: 2-second startup phase that monitors for:
  - HI or SD messages from glider
  - HW_CONF line from sensor
  - Sends status byte after 2 seconds: `SW,0:<status>`
  - Status bit 0 (LSB) = glider seen (1 = HI/SD received, 0 = not)
  - Status bit 1 (MSB) = sensor seen (1 = HW_CONF received, 0 = not)
  
- **STATE_IDLE**: Waiting for dive start commands (HI or SD messages)
  
- **STATE_PASSTHROUGH**: Bidirectional UART forwarding with $QUIT exit
  
- **STATE_DIVE**: Active dive with sensor control and mode monitoring

### 2. New Sensor Control Commands

#### Start Command (DIVE entry)
- **With valid time**: `$start:ACQ_CSCS_002H,YYYYMMDD,HHMMSS;\n`
- **Without time**: `$start:ACQ_CSCS_002H;\n`
- Time converted from glider epoch field
- Sends SW,1:1 for valid time or SW,1:-1 for no time

#### Stop Command (DIVE exit or mode change)
- Format: `$stop;\n`
- Sent **twice** with 200ms delay between sends
- Triggered by:
  - $BY message from glider (end of dive)
  - Mode change detected in SD message (dive → climb, etc.)

### 3. Mode Change Handling
- When mode changes during dive:
  1. Send two stop commands (200ms apart)
  2. Wait 2 seconds
  3. Automatically restart sensor with new time
  4. Reset sample trigger for new mode

### 4. SD Message Field Changes
**Old indices**: epoch=2, depth=3, mode=4
**New indices**: epoch=4, depth=5, mode=6

Parsing updated in `parse_sd_fields()` function.

### 5. HW_CONF Detection
- New matcher for `HW_CONF` string from sensor
- Tracked during POWERUP phase
- Line accumulation and debug output included

### 6. Comprehensive Debug Output via RTT
All major events logged with timestamps and values:
- POWERUP phase progress
- Status byte calculation
- Sensor start/stop commands with timestamps
- Mode changes and transitions
- Time formatting from epoch
- State transitions with reasons

### 7. Time Formatting
- Converts epoch (Unix timestamp) to YYYYMMDD,HHMMSS format
- Uses `gmtime()` for UTC conversion
- Integrated into start command generation

## File Changes Summary

### `glider.h`
- Added `GLIDER_SD_EXPECTED` constant for $SD detection
- Updated field definitions: epoch=4, depth=5, mode=6
- Added `POWERUP_TIMEOUT_MS` constant

### `sensor.h`
- Added `SENSOR_HW_CONF_STR` for HW_CONF detection
- Changed start command format to `SENSOR_START_NO_TIME` (parameterized)
- Added `SENSOR_STOP_DELAY_MS` (200ms) for dual stop timing
- Added status bit definitions for powerup status byte

### `main.c`
- Added STATE_POWERUP to state machine enum
- Implemented `on_u0_powerup()` and `on_u1_powerup()` handlers
- New sensor command functions:
  - `sensor_send_start_with_time()`: Format and send timed start
  - `sensor_send_start_no_time()`: Send untimed start
  - `sensor_send_stop()`: Send stop command
- Updated `send_sw_on_uart0_int()` to handle signed values (for -1)
- Modified `parse_sd_fields()` with new field indices
- Enhanced main loop with POWERUP state handling
- Added sensor restart logic after mode changes
- Comprehensive RTT debug logging throughout

## RTT Debug Examples

```
[RTT] POWERUP: HI or SD detected from glider
[RTT] POWERUP: HW_CONF detected from sensor
[RTT] POWERUP phase complete after 2000 ms
[RTT] Status: glider=seen sensor=seen status_byte=3
[RTT] Sent SW,0:3 to glider
[RTT] DIVE START: HI or SD message received
[RTT] ->U1 START (with time): $start:ACQ_CSCS_002H,20250428,143022;
[RTT] Sensor started with time, SW,1:1 sent
[RTT] Mode change detected: 1 -> 2 (stopping sensor)
[RTT] Sensor stop commands sent (mode change, x2)
[RTT] Scheduled sensor restart in 2 seconds
[RTT] Restarting sensor after mode change...
[RTT] Sensor restarted with time
```

## Testing Recommendations

1. **Powerup Phase**:
   - Verify HI/SD detection within 2 seconds
   - Verify HW_CONF detection and line parsing
   - Check status byte values (0, 1, 2, or 3)

2. **Dive Start**:
   - Verify time formatting from epoch
   - Check SW,1:1 sent for valid time
   - Check SW,1:-1 sent for no time

3. **Mode Changes**:
   - Verify stop commands sent twice (200ms apart)
   - Verify restart after 2-second wait
   - Check sample trigger reset

4. **Dive End**:
   - Verify BY triggers double stop
   - Check state transition back to IDLE
   - Verify all variables reset

5. **Passthrough**:
   - Verify $MIRROR still works from both UARTs
   - Verify $QUIT exits passthrough properly
