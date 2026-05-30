# SMaRT
**Sensor Management and Relay Tool**

This tool is designed to facilitate easier sensor integration on gliders.
The idea is to have a mainboard that hosts popular, easy-to-source modules in order to create a man-in-the-middle board for custom sensor integration or for fully using the capabilities of the Slocum BackSeatDriver without needing power-hungry boards such as the Raspberry Pi or other single-board computers.

**Hardware**

- the brain is an RP2040 board (Pi Pico)
- the RS232 transceivers are SparkFun MAX3232 modules
- the DC-DC converter is the popular MP1584 module
- (optional) OpenLog for using this board as a data logger
- (optional) solid-state relay for independent sensor power control
- Connectors:
	- Molex connectors compatible with the ones used in the Slocum Science Computer
	- 2.54 mm pitch footprints for most other connectors, so either simple headers or other locking connectors such as the JST-XH series can be used
	- 2 x Qwiic connectors for connecting external sensor modules

![front](pics/front.png)
![back](pics/back.png)

The form factor is also just small enough to fit inside a [1000 m rated 2" x 150 mm Blue Robotics housing](https://bluerobotics.com/store/watertight-enclosures/wte-vp/), so it could be used as an external interface, which was the original idea. It could also be used as a simple logger with a single end connection to a sensor.

**Firmware**

The current firmware is a work-in-progress for UVP6 integration on Slocum only. The plan is to make the firmware more generic and applicable to both Seaglider and Slocum.
