# Quick Reference - New Sensor Integration

## State Machine Transitions

```
           +--------+
           | POWERUP|  (2 seconds)
           +---+----+
               |
               v
           +--------+
    +----> | IDLE   | <----+
    |      +---+----+      |
    |          |           |
    |          | HI/SD     |
    |          v           |
    |      +--------+      |
    |      | DIVE   +------+
    |      +---+----+      BY
    |          |           |
    +----------+           +
      MIRROR or          MIRROR
      end PASSTHROUGH    or end PASSTHROUGH
```

## Key Commands Sent to Sensor

### Start Command (sent at dive start)
```
With time:    $start:ACQ_CSCS_002H,YYYYMMDD,HHMMSS;\n
Without time: $start:ACQ_CSCS_002H;\n
```

### Stop Command (sent twice with 200ms delay)
```
$stop;\n
k_msleep(200);
$stop;\n
```

## Key Messages from Glider (UART0)

| Message | Meaning | Action |
|---------|---------|--------|
| `$HI` | Dive start signal | Transition IDLE→DIVE, start sensor |
| `$SD,` | Sensor data with mode/depth | Process if IDLE (→DIVE), check mode changes in DIVE |
| `$BY` | Dive end (end of mission) | Stop sensor, transition DIVE→IDLE |
| `$MIRROR` | Enable passthrough mode | Transition to STATE_PASSTHROUGH |

## Key Messages from Sensor (UART1)

| Message | Meaning | Action |
|---------|---------|--------|
| `HW_CONF` | Hardware config (powerup) | Detected during powerup phase only |
| `$startack;` | Start command acknowledged | Logged, no action needed |
| `$stopack;` | Stop command acknowledged | Sets stop_in_progress = false |

## Response Messages (sent to Glider on UART0)

| Command | Timing | Meaning |
|---------|--------|---------|
| `$SW,0:<status>*<cs>\r\n` | End of powerup (2s) | Powerup status: bit0=glider, bit1=sensor |
| `$SW,1:1*<cs>\r\n` | Start of dive | Valid time provided to sensor |
| `$SW,1:-1*<cs>\r\n` | Start of dive | No valid time to sensor |
| `$SW,1:1*<cs>\r\n` | After mode restart (2s) | Restarted sensor with valid time |
| `$SW,1:-1*<cs>\r\n` | After mode restart (2s) | Restarted sensor without time |

## Mode Change Behavior

When glider SD message shows mode change (e.g., 1→2):
1. **t+0ms**: Mode change detected
2. **t+0ms**: Send first stop command
3. **t+200ms**: Send second stop command
4. **t+200ms**: Mark sensor_restart_pending = true
5. **t+2200ms**: Automatically restart sensor with appropriate start command

## Powerup Status Byte

Calculated and sent after 2 seconds from powerup:
- **Bit 0 (LSB)**: 1 if HI or SD seen, else 0
- **Bit 1 (MSB)**: 1 if HW_CONF seen, else 0
- **Result**: 0 (none), 1 (glider only), 2 (sensor only), or 3 (both)

## Time Formatting

Epoch (Unix timestamp in seconds) → `YYYYMMDD,HHMMSS` (GMT/UTC)

Example:
- Epoch: 1234567890 
- Formatted: 20090213,233130 (2009-02-13 23:31:30)

## RTT Debug Output Locations

Enable with: `#define DEBUG_RTT 1` (already enabled)

Key debug points:
- All state transitions logged
- All UART command sends logged
- Mode changes logged
- Powerup phase progress logged
- Status byte calculation logged
- Sensor start/stop timings logged

## Error Handling

- **Unsigned chars for negative SW values**: Updated to use signed int32_t
- **Double stop timing**: 200ms delay enforced via k_msleep()
- **Mode restart delay**: 2 second delay enforced via time comparison
- **Matcher reset**: Matchers properly reset in powerup phase
- **Line accumulation**: 512-byte buffers with stall detection

## Testing Checklist

- [ ] Powerup phase detects HI/SD within 2 seconds
- [ ] Powerup phase detects HW_CONF line within 2 seconds
- [ ] Correct status byte sent after 2 seconds
- [ ] Sensor starts with time when epoch available
- [ ] Sensor starts without time when epoch unavailable
- [ ] SW,1:1 sent for valid time
- [ ] SW,1:-1 sent for no time
- [ ] Mode changes trigger double stop (200ms apart)
- [ ] Sensor restarts after 2 second delay following mode change
- [ ] BY message triggers double stop and transition to IDLE
- [ ] Passthrough mode ($MIRROR) still works
- [ ] $QUIT exits passthrough correctly
- [ ] Time conversion works (check RTT output format)
