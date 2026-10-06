# Documentation Index

This folder contains comprehensive documentation for the multi-sensor integration firmware.

## Quick Start

**New to this project?** Start here:
1. Read `QUICK_REFERENCE.md` for a 2-minute overview
2. Review `CHANGES_SUMMARY.md` for what was modified
3. Check `STATUS_BYTE_ENCODING.md` if working with powerup phase

## Documentation Files

### `CHANGES_SUMMARY.md`
**Purpose:** High-level overview of all code changes  
**Content:**
- List of modified files (glider.h, sensor.h, main.c)
- Description of each change and rationale
- New features implemented
- Testing strategy
- Build instructions
- Known limitations
- Future enhancement ideas

**Best for:** Understanding what was changed and why

---

### `QUICK_REFERENCE.md`
**Purpose:** Quick lookup guide for common operations  
**Content:**
- State machine transitions diagram
- Command reference table (glider ↔ sensor)
- Key messages and meanings
- Response messages sent to glider
- Mode change behavior walkthrough
- Status byte quick reference
- RTT debug output locations
- Testing checklist

**Best for:** Quick lookups during development/testing

---

### `STATUS_BYTE_ENCODING.md`
**Purpose:** Detailed documentation of powerup status byte  
**Content:**
- Bit-level encoding explanation
- Status byte value table (0-3)
- Example scenarios for each value
- Calculation algorithm
- XOR checksum details
- Timeline diagram

**Best for:** Understanding powerup phase and status reporting

---

### `PROTOCOL_REFERENCE.md`
**Purpose:** Complete message format and protocol specification  
**Content:**
- System architecture overview
- Detailed powerup phase flow (timeline and behavior)
- IDLE state behavior and transitions
- DIVE state behavior and transitions
- Mode change handling details
- All message formats (inbound/outbound)
- Time conversion examples
- Passthrough mode documentation
- RTT debug output examples
- Error handling table
- Performance characteristics
- Complete state diagram

**Best for:** Understanding the complete system behavior

---

### `IMPLEMENTATION_NOTES.md`
**Purpose:** Technical implementation details  
**Content:**
- State machine expansion summary
- Sensor control command details
- Mode change handling explanation
- SD field index changes
- HW_CONF detection mechanism
- Comprehensive debug output locations
- Time formatting algorithm
- File change summary for each source file

**Best for:** Understanding implementation details and code structure

---

## Feature Breakdown

### Powerup Phase (2 seconds)
- **Files:** POWERUP phase enum, on_u0_powerup(), on_u1_powerup()
- **Docs:** STATUS_BYTE_ENCODING.md, PROTOCOL_REFERENCE.md
- **Status:** Monitors for HI/SD/HW_CONF, sends status byte

### Sensor Start Command
- **Files:** sensor_send_start_with_time(), sensor_send_start_no_time()
- **Docs:** PROTOCOL_REFERENCE.md section "Message Formats"
- **Status:** Formats epoch as YYYYMMDD,HHMMSS, sends SW,1

### Sensor Stop Command
- **Files:** sensor_send_stop() (called twice with 200ms delay)
- **Docs:** QUICK_REFERENCE.md, PROTOCOL_REFERENCE.md
- **Status:** Triggered by BY or mode change

### Mode Change Handling
- **Files:** on_u0_run() mode detection, sensor restart logic
- **Docs:** QUICK_REFERENCE.md, PROTOCOL_REFERENCE.md
- **Status:** Automatic stop/wait/restart with 2-second delay

### Time Conversion
- **Files:** sensor_send_start_with_time() using gmtime()
- **Docs:** PROTOCOL_REFERENCE.md, IMPLEMENTATION_NOTES.md
- **Status:** Unix epoch → YYYYMMDD,HHMMSS (UTC)

### Debug Output
- **Files:** #if DEBUG_RTT throughout main.c
- **Docs:** All docs reference RTT output examples
- **Status:** Comprehensive logging to RTT console

---

## Key Diagrams

### State Machine
See: `QUICK_REFERENCE.md` (ASCII diagram)  
See: `PROTOCOL_REFERENCE.md` (complete diagram with details)

### Powerup Timeline
See: `PROTOCOL_REFERENCE.md` section "Powerup Phase (0-2 seconds)"

### Mode Change Sequence
See: `PROTOCOL_REFERENCE.md` section "Mode Change Handling"

---

## Message Reference Quick Lookup

### From Glider (UART0)
| Message | Reference |
|---------|-----------|
| `$HI` | QUICK_REFERENCE.md, PROTOCOL_REFERENCE.md |
| `$SD,` | QUICK_REFERENCE.md, PROTOCOL_REFERENCE.md |
| `$BY` | QUICK_REFERENCE.md, PROTOCOL_REFERENCE.md |
| `$MIRROR` | QUICK_REFERENCE.md, PROTOCOL_REFERENCE.md |

### To Glider (UART0)
| Message | Reference |
|---------|-----------|
| `$SW,0:<status>` | STATUS_BYTE_ENCODING.md, PROTOCOL_REFERENCE.md |
| `$SW,1:1` | QUICK_REFERENCE.md, PROTOCOL_REFERENCE.md |
| `$SW,1:-1` | QUICK_REFERENCE.md, PROTOCOL_REFERENCE.md |

### To Sensor (UART1)
| Message | Reference |
|---------|-----------|
| `$start:...` | PROTOCOL_REFERENCE.md |
| `$stop;` | QUICK_REFERENCE.md, PROTOCOL_REFERENCE.md |

---

## Testing Checklist

See: `QUICK_REFERENCE.md` section "Testing Checklist"

For detailed testing strategy, see: `CHANGES_SUMMARY.md` section "Testing Strategy"

---

## For Code Review

1. Start with: `CHANGES_SUMMARY.md`
2. Review changes in order:
   - glider.h (constants)
   - sensor.h (constants)
   - main.c (functions, then main loop)
3. Check implementation against: `PROTOCOL_REFERENCE.md`
4. Verify RTT output matches: `QUICK_REFERENCE.md`

---

## For Integration Testing

1. Review: `PROTOCOL_REFERENCE.md` (complete message flows)
2. Monitor: RTT output (examples in all docs)
3. Verify: `QUICK_REFERENCE.md` checklist items
4. Cross-check: Message formats in `PROTOCOL_REFERENCE.md`

---

## Build & Flash

```bash
cd /home/glider/zephyrproject/SMaRT_UVP6_v1
west build
west flash  # If connected to RPi Pico
```

Then monitor RTT console for debug output.

---

## File Locations

```
SMaRT_UVP6_v1/
├── CMakeLists.txt
├── prj.conf
├── boards/
│   └── rpi_pico.overlay
├── src/
│   ├── main.c                      (MODIFIED)
│   ├── glider.h                    (MODIFIED)
│   └── sensor.h                    (MODIFIED)
└── Documentation (NEW):
    ├── CHANGES_SUMMARY.md          (This folder)
    ├── QUICK_REFERENCE.md
    ├── STATUS_BYTE_ENCODING.md
    ├── PROTOCOL_REFERENCE.md
    ├── IMPLEMENTATION_NOTES.md
    └── README.md                   (This file)
```

---

## Questions?

Refer to the appropriate documentation file based on your question type:

- **"What changed?"** → CHANGES_SUMMARY.md
- **"How do I...?"** → QUICK_REFERENCE.md
- **"What does this status byte mean?"** → STATUS_BYTE_ENCODING.md
- **"Show me message format XYZ"** → PROTOCOL_REFERENCE.md
- **"How does feature ABC work?"** → IMPLEMENTATION_NOTES.md

---

## Version Information

- **Firmware Version:** Multi-Sensor Integration v1.0
- **Last Updated:** November 28, 2025
- **Compatible Hardware:** RPi Pico with dual UART support
- **Zephyr Kernel:** See prj.conf for version
- **Languages:** C99 (Zephyr compatible)

---
