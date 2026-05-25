# Status Byte Encoding (Powerup Phase)

## Overview
After 2 seconds of powerup monitoring, the system calculates and sends a status byte to the glider via UART0 using the command:
```
$SW,0:<status_byte>*<checksum>\r\n
```

## Bit Mapping

```
Status Byte (8-bit):  [7][6][5][4][3][2][1][0]
                                        MSB LSB
                                        
Bit 1 (MSB): Sensor Status
  - 1 = HW_CONF message received from sensor during powerup
  - 0 = HW_CONF message NOT received

Bit 0 (LSB): Glider Status  
  - 1 = HI or SD message received from glider during powerup
  - 0 = HI or SD message NOT received

Bits [7:2]: Reserved (always 0)
```

## Status Byte Values

| Decimal | Binary | Glider | Sensor | Meaning |
|---------|--------|--------|--------|---------|
| 0 | 0000 0000 | No | No | Neither device detected |
| 1 | 0000 0001 | Yes | No | Only glider detected |
| 2 | 0000 0010 | No | Yes | Only sensor detected |
| 3 | 0000 0011 | Yes | Yes | Both devices detected |

## Implementation Details

### Status Byte Calculation
```c
uint8_t status_byte = 0;
if (powerup_sensor_seen) {
    status_byte |= (1 << STATUS_SENSOR_BIT);  // Set bit 1
}
if (powerup_glider_seen) {
    status_byte |= (1 << STATUS_GLIDER_BIT);  // Set bit 0
}
```

### Signal Detection Criteria
- **Glider**: Either `$HI` or `$SD` message detected in UART0 data stream
- **Sensor**: `HW_CONF` substring detected in UART1 data stream

### Powerup Sequence Timeline
```
t=0ms    : Board powers up
           STATE_POWERUP entered
           Matchers initialized
           Monitoring begins
           
t=0-2000ms: UART data monitored
           powerup_glider_seen updated if HI/SD seen
           powerup_sensor_seen updated if HW_CONF seen
           
t=2000ms : Powerup timeout
           Status byte calculated
           $SW,0:<status> sent to glider
           Transition to STATE_IDLE
```

## Example Scenarios

### Scenario 1: Both Devices Ready
```
t=500ms   : Glider sends $HI
            powerup_glider_seen = true
t=750ms   : Sensor sends HW_CONF,000...
            powerup_sensor_seen = true
t=2000ms  : Powerup complete
            status_byte = 0b00000011 = 3
            Send: $SW,0:3*<cs>\r\n
```

### Scenario 2: Only Glider Ready
```
t=1500ms  : Glider sends $SD,... message
            powerup_glider_seen = true
t=2000ms  : Powerup complete (sensor never seen)
            status_byte = 0b00000001 = 1
            Send: $SW,0:1*<cs>\r\n
```

### Scenario 3: No Devices Detected
```
t=2000ms  : Powerup complete (no messages received)
            powerup_glider_seen = false
            powerup_sensor_seen = false
            status_byte = 0b00000000 = 0
            Send: $SW,0:0*<cs>\r\n
```

## XOR Checksum Calculation
The checksum is calculated on the payload string (SW,0:X):
```c
uint8_t cs = 0;
for each character c in "SW,0:X":
    cs ^= c;
```
Then appended as two hex digits: `*<hex_hi><hex_lo>`

## RTT Debug Output Example
```
[RTT] POWERUP phase complete after 2000 ms
[RTT] Status: glider=seen sensor=seen status_byte=3
[RTT] ->U0 SW: $SW,0:3*05\r\n
[RTT] Sent SW,0:3 to glider
[RTT] Transitioning to IDLE state
```
