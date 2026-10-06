# Complete Protocol Documentation - Multi-Sensor Integration

## System Overview

This firmware supports a two-UART system:
- **UART0**: Glider communication (Slocum backseat driver protocol)
- **UART1**: New sensor communication (ACQ_CSCS_002H protocol)

The board powers up with minimal power consumption and monitors for commands from both devices during a 2-second powerup phase.

---

## Powerup Phase (0-2 seconds)

### Timeline
```
t=0ms     : Board powers up
            STATE_POWERUP entered
            matchers & line accumulators initialized
            powerup_glider_seen = false
            powerup_sensor_seen = false
            powerup_start_time = current_time

t=0-2000ms: Continuous monitoring
            on_u0_powerup() processes UART0 bytes
            on_u1_powerup() processes UART1 bytes
            
            When HI or SD detected: powerup_glider_seen = true
            When HW_CONF detected: powerup_sensor_seen = true

t=2000ms  : Powerup phase ends
            Status byte calculated:
              bit 0 = powerup_glider_seen ? 1 : 0
              bit 1 = powerup_sensor_seen ? 1 : 0
            $SW,0:<status>*<cs>\r\n sent to glider
            Transition to STATE_IDLE
```

### Powerup Monitoring

**UART0 (Glider) Monitoring:**
- Matcher for "$HI" string (glider dive announcement)
- Matcher for "$SD" string (glider sensor data message)
- Either match sets powerup_glider_seen = true
- Debug message logged on detection

**UART1 (Sensor) Monitoring:**
- Matcher for "HW_CONF" string (sensor hardware config)
- Full line accumulation and parsing
- Setting powerup_sensor_seen = true
- Complete line logged to RTT when detected

### Status Byte Calculation

```c
// Status byte encoding:
// Bit 1 (MSB) = Sensor status (1 = HW_CONF seen)
// Bit 0 (LSB) = Glider status (1 = HI/SD seen)

uint8_t status_byte = 0;
if (powerup_sensor_seen)  status_byte |= (1 << 1);  // 0b00000010 = 2
if (powerup_glider_seen)  status_byte |= (1 << 0);  // 0b00000001 = 1

// Results:
// 0 = neither device ready
// 1 = glider ready only
// 2 = sensor ready only
// 3 = both devices ready
```

### UART0 Response: Status Byte

Format: `$SW,0:<status>*<checksum>\r\n`

Example responses:
- `$SW,0:0*00\r\n` - No devices detected
- `$SW,0:1*01\r\n` - Glider detected only
- `$SW,0:2*03\r\n` - Sensor detected only
- `$SW,0:3*02\r\n` - Both detected

**Checksum**: XOR of all bytes in payload (SW,0:X)

---

## IDLE State (after powerup)

### Entry Conditions
- Powerup phase complete with status byte sent
- $BY received from glider (end of dive)
- $QUIT received from passthrough mode

### Behavior
- Poll both UARTs for incoming data
- Process one message at a time
- Look for state transition triggers

### State Transition Triggers

**→ STATE_PASSTHROUGH**
- Receive "$MIRROR" on UART0 OR UART1
- Both directions fully forwarded until $QUIT

**→ STATE_DIVE**
- Receive "$HI" message from glider (UART0)
- Receive "$SD,..." message from glider (UART0)

---

## DIVE State (active dive)

### Entry Actions
1. Determine if valid epoch available from previous SD messages
2. Send sensor start command:
   - With time: `$start:ACQ_CSCS_002H,YYYYMMDD,HHMMSS;\n`
   - Without: `$start:ACQ_CSCS_002H;\n`
3. Send SW,1 status:
   - `$SW,1:1*<cs>\r\n` if time provided
   - `$SW,1:-1*<cs>\r\n` if no time

### State Transitions

**→ STATE_IDLE**
- Receive "$BY" message from glider
- Triggers double stop sequence (200ms apart)

### Mode Change Handling

**Detection:**
- Parse SD message field 6 (mode: 1=dive, 2=climb, 3=hover)
- Compare with last_mode
- If different and not already stopping, trigger stop sequence

**Stop Sequence:**
1. Send first `$stop;\n`
2. Wait 200ms (SENSOR_STOP_DELAY_MS)
3. Send second `$stop;\n`
4. Set stop_in_progress = true
5. Schedule restart: sensor_restart_time = now + 2000ms

**Restart Sequence (after 2 second wait):**
1. Send start command (with or without time as appropriate)
2. Send SW,1 status
3. Reset sample_triggered flag

---

## Message Formats

### UART0 (Glider) - Inbound

| Format | Example | Triggers |
|--------|---------|----------|
| `$HI` | `$HI*XX\r\n` | Dive start |
| `$SD,` | `$SD,4:1234567.890,5:42.3,6:1*XX\r\n` | Dive data, mode change check |
| `$BY` | `$BY*XX\r\n` | Dive end, sensor stop |
| `$MIRROR` | `$MIRROR*XX\r\n` | Enter passthrough |

**SD Message Field Mapping (NEW):**
- Field 4: Epoch time (Unix timestamp, seconds)
- Field 5: Depth (meters, float)
- Field 6: Mode (1=dive, 2=climb, 3=hover)

Format per field: `N:value` where N is field number

### UART0 (Glider) - Outbound

| Format | When | Meaning |
|--------|------|---------|
| `$SW,0:<status>*<cs>\r\n` | After powerup (2s) | Device readiness status |
| `$SW,1:1*<cs>\r\n` | At dive start | Epoch time provided to sensor |
| `$SW,1:-1*<cs>\r\n` | At dive start | No epoch time available |
| `$SW,1:1*<cs>\r\n` | After mode restart | Restarted with time |
| `$SW,1:-1*<cs>\r\n` | After mode restart | Restarted without time |

### UART1 (Sensor) - Inbound

| Format | When | Action |
|--------|------|--------|
| `HW_CONF,000...` | Powerup phase | Detected as device ready |
| `$startack;` | After start sent | Logged to RTT only |
| `$stopack;` | After stop sent | Sets stop_in_progress = false |

### UART1 (Sensor) - Outbound

| Format | When | Purpose |
|--------|------|---------|
| `$start:ACQ_CSCS_002H,YYYYMMDD,HHMMSS;\n` | Dive start | Start sampling with time |
| `$start:ACQ_CSCS_002H;\n` | Dive start | Start sampling without time |
| `$stop;\n` (x2) | Dive end or mode change | Stop sampling (sent twice) |

---

## Time Conversion Example

Unix Epoch → YYYYMMDD,HHMMSS (GMT/UTC)

```c
time_t epoch_t = (time_t)1234567890;
struct tm *time_info = gmtime(&epoch_t);

// Produces: 20090213,233130
// Date: February 13, 2009
// Time: 23:31:30 UTC
```

Command sent: `$start:ACQ_CSCS_002H,20090213,233130;\n`

---

## Passthrough Mode

### Entry
Receive "$MIRROR" on either UART (in STATE_IDLE)

### Operation
- Full bidirectional forwarding
- No parsing or filtering
- Runs at maximum speed (no artificial delays)
- Monitors for $QUIT command to exit

### Exit
- Receive "$QUIT" on either UART
- Returns to STATE_IDLE
- Drains and resets line accumulators

---

## RTT Debug Output Reference

### Powerup Phase
```
[RTT] ===== MAIN START =====
[RTT] Board initialized, entering POWERUP state
[RTT] UART0 (glider) ready: YES
[RTT] UART1 (sensor) ready: YES
[RTT] System initialized. Starting POWERUP phase...
[RTT] POWERUP: HI or SD detected from glider
[RTT] POWERUP: HW_CONF detected from sensor
[RTT] POWERUP phase complete after 2000 ms
[RTT] Status: glider=seen sensor=seen status_byte=3
[RTT] ->U0 SW: $SW,0:3*02\r\n
[RTT] Sent SW,0:3 to glider
[RTT] Transitioning to IDLE state
```

### Dive Start
```
[RTT] DIVE START: HI or SD message received
[RTT] ->U1 START (with time): $start:ACQ_CSCS_002H,20090213,233130;
[RTT] Sensor started with time, SW,1:1 sent
[RTT] ->U0 SW: $SW,1:1*01\r\n
```

### Mode Change
```
[RTT] Mode change detected: 1 -> 2 (stopping sensor)
[RTT] ->U1 STOP: $stop;
[RTT] Sensor stop commands sent (mode change, x2)
[RTT] Scheduled sensor restart in 2 seconds
[RTT] Restarting sensor after mode change...
[RTT] ->U1 START (with time): $start:ACQ_CSCS_002H,...
[RTT] Sensor restarted with time
```

### Dive End
```
[RTT] $BY received, ending dive. Stopping sensor...
[RTT] ->U1 STOP: $stop;
[RTT] Sensor stop commands sent (x2)
[RTT] Transitioning back to IDLE
```

---

## Error Handling

| Condition | Handling |
|-----------|----------|
| UART not ready | Infinite loop with 1s delays, RTT error message |
| Parse errors in SD | Fields with parse errors left as previous value |
| Invalid epoch (0) | No time sent to sensor, SW,1:-1 used |
| Stop command buffer overflow | Gracefully skipped, logged to RTT |
| Line buffer overflow | Line stalled, flushed, next line begins |

---

## Performance Characteristics

- **Powerup detection latency**: < 1 byte time @ 115200 baud
- **Mode change latency**: 200ms (dual stop timing enforced)
- **Restart delay**: 2000ms ± 2ms (k_msleep granularity)
- **Passthrough latency**: < 1 byte time (no buffering, direct forward)
- **CPU utilization**: 2ms sleep in IDLE/DIVE, full speed in PASSTHROUGH
- **UART bitrate**: 115200 baud (as defined)
- **Max line length**: 512 bytes (stall at 150ms)

---

## State Diagram

```
    ┌─────────────┐
    │  POWERUP    │ (2 seconds)
    │  (startup)  │
    └──────┬──────┘
           │
           v
    ┌─────────────┐      ┌────────────┐
    │    IDLE     │◄────►│PASSTHROUGH │
    │  (waiting)  │  $MI │ (forwarding)
    └──┬──────┬──┘  $QUI └────────────┘
       │      │
    $HI│      │$SD
       │      │
       v      v
    ┌─────────────┐
    │    DIVE     │
    │  (active)   │
    └──────┬──────┘
           │
        $BY│
           │
           v
        IDLE
```
