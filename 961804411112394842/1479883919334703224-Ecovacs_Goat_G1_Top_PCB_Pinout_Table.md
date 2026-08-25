
**Top MCU Board:**

Components:
- GD32F103C (MCU for Buttons, LCD, Rain Sensor, Optional GPS)
- LCD - ST7565R/UC1701
- GD25Q80ETIG 8Mbit SPI Flash
- HMC5883L 3 Axis IMU
- Optional GPS Module

Header 1 - Interface & Sensors:

| PIN row 1                                | PIN Row 2 |
| ---------------------------------------- | --------------------------- |
| 1: GND                                   | 2: 5V                       |
| 3: (3.3V) MCU UART TX 30 Ohm to Main P13 | 4: Power Button (35 Ohm)    |
| 5: 3.3V MCU UART RX                      | 6: 30 Ohm to Top P21 / 1.7V |
| 7: GND (Only on PWR Board)               | 8: GND                      |
| 9: 4.4V (Stop Hall 1)                    | 10: 4.4V (Stop Hall 2)      |
| 11: SDA (HP5883 3641)                    | 12: SCL (HP5883 3641)       |


Header 2 - Programming:

|  PIN row 1   | PIN row 2  |
| ------------ | ---------- |
| 1 ?          | 2 ?        |
| 3 ?          | 4 ?        |
| 5 PA14 SWCLK | 6 ?        |
| 7 PA13 SWDIO | 8 ?        |
