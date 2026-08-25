# Top MCU Pin Definitions (GD32F103C8T6)

Source: `top_mcu_original.bin` loaded at `0x08003000`.  
Firmware MD5: `f7ecef7cc2096fac4b798c883c6dbcf6`  
Notes: Input pull-up implies active-low buttons unless stated otherwise.

---

## Pin Summary Table

| Pin  | Function         | Direction | Mode           | Init Function              |
|------|------------------|-----------|----------------|----------------------------|
| PA0  | LCD_BACKLIGHT_EN | Output    | Push-pull 2MHz | `gpio_init_display_pins`   |
| PA1  | RAIN_SENSOR      | Input     | Pull-up        | `rain_sensor_detect_pa1`   |
| PA2  | USART2_TX        | Output    | AF Push-pull   | `usart2_init`              |
| PA3  | USART2_RX        | Input     | Floating       | `usart2_init`              |
| PA4  | LCD_CS           | Output    | Push-pull 2MHz | `gpio_init_display_pins`   |
| PA5  | LCD_SCK          | Output    | Push-pull 2MHz | `gpio_init_display_pins`   |
| PA7  | LCD_MOSI         | Output    | Push-pull 2MHz | `gpio_init_display_pins`   |
| PA9  | USART1_TX        | Output    | AF Push-pull   | `usart1_init`              |
| PA10 | USART1_RX        | Input     | Floating       | `usart1_init`              |
| PA11 | (USB_D-)         | -         | Not configured | -                          |
| PA12 | (USB_D+)         | -         | Not configured | -                          |
| PA15 | BTN_7            | Input     | Pull-up        | `gpio_init_button_pins`    |
| PB0  | LCD_RST          | Output    | Push-pull 2MHz | `gpio_init_display_pins`   |
| PB1  | LCD_DC (A0)      | Output    | Push-pull 2MHz | `gpio_init_display_pins`   |
| PB3  | BTN_0            | Input     | Pull-up        | `gpio_init_button_pins`    |
| PB4  | BTN_1            | Input     | Pull-up        | `gpio_init_button_pins`    |
| PB5  | BTN_2            | Input     | Pull-up        | `gpio_init_button_pins`    |
| PB6  | BTN_3            | Input     | Pull-up        | `gpio_init_button_pins`    |
| PB7  | BTN_4            | Input     | Pull-up        | `gpio_init_button_pins`    |
| PB8  | BTN_5            | Input     | Pull-up        | `gpio_init_button_pins`    |
| PB9  | BTN_6            | Input     | Pull-up        | `gpio_init_button_pins`    |
| PB12 | SPI2_NSS         | Output    | Push-pull 2MHz | `spi2_init`                |
| PB13 | SPI2_SCK         | Output    | AF Push-pull   | `spi2_init`                |
| PB14 | SPI2_MISO        | Input     | AF Floating    | `spi2_init`                |
| PB15 | SPI2_MOSI        | Output    | AF Push-pull   | `spi2_init`                |

---

## Display Interface (Bit-banged SPI, ST7565/UC1701)

| Pin | Signal   | Description                        | IDA Reference                         |
|-----|----------|------------------------------------|---------------------------------------|
| PA4 | LCD_CS   | Chip Select (active low)           | GPIOA BSRR/BRR in `display_send_byte` |
| PA5 | LCD_SCK  | Serial Clock                       | Bit-banged in `spi_bitbang_tx_byte`   |
| PA7 | LCD_MOSI | Master Out Slave In                | Bit-banged in `spi_bitbang_tx_byte`   |
| PB0 | LCD_RST  | Reset (active low)                 | Toggled in `display_init_st7565`      |
| PB1 | LCD_DC   | Data/Command (A0): 0=cmd, 1=data   | Set in `display_send_byte`            |
| PA0 | LCD_EN   | Backlight enable (set HIGH on init)| `gpio_init_display_pins`              |

**Display Init Sequence** (`display_init_st7565` @ `0x8008918`):
```
0xE2 - Software Reset
0xA2 - LCD Bias 1/9
0xA0 - ADC Normal
0xC8 - COM Output Reverse
0x24 - V0 Voltage Resistor Ratio
0x81, 0x3F - Electronic Volume (Contrast)
0x2C - Power Control (Booster ON)
0x2E - Power Control (Voltage Regulator ON)
0x2F - Power Control (Voltage Follower ON)
0xAF - Display ON
```

**Display Control Commands** (via SA Protocol):
| Command | ST7565 Opcode | Description |
|---------|---------------|-------------|
| 0xAF    | Display ON    | Turn display on |
| 0xAE    | Display OFF   | Turn display off |
| 0xA5    | All Points ON | All pixels lit |
| 0xA4    | Normal Mode   | Normal display |
| 0xA7    | Inverse ON    | Inverted display |
| 0xA6    | Inverse OFF   | Normal polarity |

Implementation notes (sa_command_handler):
- `@Ea01*` / `@Ea00*` -> `display_send_byte(0xAF/0xAE)`
- `@Fa01*` / `@Fa00*` -> `display_send_byte(0xA5/0xA4)`
- `@Ga01*` / `@Ga00*` -> `display_send_byte(0xA7/0xA6)`
- `@Ha01*` / `@Ha00*` toggles PA0 via GPIOA BSRR/BRR (backlight)

---

## UART Interfaces

### USART2 (Main communication - to Linux SBC)

| Register | Address      | Value       |
|----------|--------------|-------------|
| Base     | 0x40004400   |             |
| BRR      | 0x40004408   | 115200 baud |

- **PA2**: TX
- **PA3**: RX
- Init: `usart2_init` @ `0x800A88C`
- IRQ Handler: `usart2_irq_handler` @ `0x800AB96`
- RCC Code: 1809 (0x711)

**Protocol:** ASCII-wrapped SA Protocol (see UART Command Protocol section below)

### USART1 (Secondary - debug?)

| Register | Address      | Value      |
|----------|--------------|------------|
| Base     | 0x40013800   |            |
| BRR      | 0x40013808   | 0x2580     |

- **PA9**: TX
- **PA10**: RX
- Init: `usart1_init` @ `0x801F83C`
- IRQ Handler: `usart1_irq_handler_nmea` @ `0x801FACA` (collects NMEA `$...*HH\r\n`, used for screen/display passthrough)
- Purpose: GPS NMEA ingress (parsed/filtered into `g_nmea_*` buffers)
- RCC Code: 1550 (0x60E)
- Note: Exact baud rate depends on APB2 clock frequency

---

## UART Command Protocol (ASCII Wrapper for SA Protocol)

The Top MCU uses an ASCII-based wrapper protocol for communication with the Linux SBC.
This wraps the SA Protocol messages documented in `SA_PROTOCOL_DOCUMENTATION.md`.

### Frame Format

```
@<CMD><PAYLOAD>*
│  │      │     └─ Frame End (0x2A = '*')
│  │      └─ Hex-encoded payload bytes
│  └─ 2-char command ID (e.g. "Bc", "Da", "Ka")
└─ Frame Start (0x40 = '@')
```

### FCT Mode (Factory Test) Entry Sequence

Special detection sequence in IRQ handler (`usart2_irq_handler`):
```
@Bc01* → Enters FCT mode (g_fct_mode_active = 1)
```

State machine: `@` → `B` → `c` → `0` → `1` → `*` = FCT Mode Active

### RX Commands (Linux SBC → Top MCU)

Processed in `sa_command_handler` @ `0x801B178`:

Notes:
- All RX commands below (except `Bc`) are accepted only when `g_display_mode == 1` (FCT active).
- `g_display_mode` is a UI render gate: 0 = normal render, 1 = FCT, 2 = update mode (see `sub_801EF20`).
- `@Da01*` sets `g_mode_display = 1` and clears `g_mode_keyevent`, `g_mode_relay`, `g_mode_screen`.
- `@Da00*` clears only `g_mode_display` (other mode flags unchanged).

| Cmd | ASCII | Payload | Description | Display Command |
|-----|-------|---------|-------------|-----------------|
| Bc  | 66,99 | 01/00   | FCT Mode ON/OFF | - |
| Da  | 68,97 | 01/00   | Display Mode ON/OFF | Sets `g_mode_display` |
| Ea  | 69,97 | 01/00   | Display Power ON/OFF | 0xAF / 0xAE |
| Fa  | 70,97 | 01/00   | All Points ON/OFF | 0xA5 / 0xA4 |
| Ga  | 71,97 | 01/00   | Inverse Display ON/OFF | 0xA7 / 0xA6 |
| Ha  | 72,97 | 01/00   | Backlight ON/OFF | PA0 HIGH/LOW |
| Ka  | 75,97 | 01/00   | Key Event Mode ON/OFF | Sets `g_mode_keyevent` |
| Ra  | 82,97 | 01/00   | Relay Mode ON/OFF | Sets `g_mode_relay` (see below) |
| Sa  | 83,97 | 01/00   | Screen Mode ON/OFF | Sets `g_mode_screen` |
| Va  | 86,97 | -       | Get Version | Response with version string |
| Pa  | 80,97 | -       | Get Protocol Info | Response with protocol info |

Display/Screen mode payload (USART1 passthrough):
- USART1 RX collects NMEA sentences in the form `$...*HH\r\n` (max 128 bytes).
- `@Sa01*` (`g_mode_screen = 1`) forwards all collected NMEA sentences to the SBC via USART2 as raw ASCII (no `@..*` wrapper).
- `@Da01*` (`g_mode_display = 1`) forwards only NMEA sentences where byte index 5 is `'L'` (e.g. `$GPGLL`).

### TX Commands (Top MCU -> Linux SBC)

| Cmd | ASCII | Payload | Description | SA Protocol Equivalent |
|-----|-------|---------|-------------|------------------------|
| Ka  | 75,97 | keycode | Key Press Event | KA message |

**Key Event Frame Example:**
```
@KaA0* → Key 0xA0 (PB7) pressed
@KaA3* → Key 0xA3 (PB3) pressed
```

### Relay Mode (`@Ra`) - Rain Sensor Passthrough

Relay Mode is a **debug/passthrough mode** for the Rain Sensor (PA1). Instead of the MCU processing the rain sensor internally, it **relays** the sensor state to the Linux SBC.

**Activation:**
```
TX (SBC → MCU): @Ra01*    → Enable Relay Mode
TX (SBC → MCU): @Ra00*    → Disable Relay Mode
```

When enabled:
- All other modes are disabled (display, keyevent, screen = 0)
- `g_mode_relay = 1`
- MCU periodically sends reset button state back to SBC

**How it works:**

1. **Rain Sensor Detection** (`rain_sensor_detect_pa1` @ `0x8008EEA`):
   - Reads PA1 (Rain Sensor, active-low = water detected)
   - Counts samples with PA1 LOW over 2000 samples (10 sec window)
   - Sets `g_rain_sensor_state = 1` if >600 samples were LOW (~30% threshold)

2. **Status Transmission** (`relay_mode_send_rain_state` @ `0x801B7C0`):
   - Called periodically from main loop when `g_mode_relay == 1`
   - Sends `@Ra<state>*\r\n` where state = 00 or 01

**Protocol:**
| Direction | Frame | Description |
|-----------|-------|-------------|
| RX | `@Ra01*` | Enable Relay Mode |
| RX | `@Ra00*` | Disable Relay Mode |
| TX | `@Ra00*` | Rain NOT detected (PA1 mostly HIGH) |
| TX | `@Ra01*` | Rain DETECTED (PA1 >30% LOW) |

**Use Cases:**
- Factory Testing: SBC monitors rain sensor directly
- Custom Firmware: SBC takes over rain detection logic
- Debugging: Raw sensor state monitoring

---

### Mapping to SA Protocol

The ASCII protocol on USART2 is a simplified wrapper around SA Protocol messages.
The Linux SBC's `protocol::SaMsgHandler` handles the conversion between:
- SA Protocol binary frames (as documented in `SA_PROTOCOL_DOCUMENTATION.md`)
- ASCII frames sent to/from the Top MCU

| ASCII Cmd | SA Protocol Msg | Direction |
|-----------|-----------------|-----------|
| Ka        | KA (Key Action) | MCU → SBC |
| Va        | VB (Version Board) | MCU → SBC |
| Ea, Fa, Ga, Ha | ZG (Screen Status) | SBC → MCU |
| Da, Ka, Ra, Sa | Mode control | SBC → MCU |

---

## Button Inputs (Active-low, Internal Pull-up)

Configured in `gpio_init_button_pins` @ `0x8008D04` with GPIO mode `0x48` (input pull-up).

| Pin  | Keycode | Button Index | Debounce Handler    |
|------|---------|--------------|---------------------|
| PB7  | 0xA0    | BTN_4        | `key_debounce_scan` |
| PB8  | 0xA1    | BTN_5        | `key_debounce_scan` |
| PB6  | 0xA2    | BTN_3        | `key_debounce_scan` |
| PB3  | 0xA3    | BTN_0        | `key_debounce_scan` |
| PB5  | 0xA4    | BTN_2        | `key_debounce_scan` |
| PB4  | 0xA5    | BTN_1        | `key_debounce_scan` |
| PB9  | 0xA6    | BTN_6        | `key_debounce_scan` |
| PA15 | 0xA7    | BTN_7        | `key_debounce_scan` |
| PA1  | -       | RAIN_SENSOR  | `rain_sensor_detect_pa1` @ `0x8008EEA`        |

**Key Event Flow:**
1. `key_debounce_scan` @ `0x8008E46` - Reads all button GPIOs, applies debounce
2. `key_update_keycodes` @ `0x8008F44` - Generates keycodes (0xA0-0xA7)
3. `send_pending_key_event` @ `0x801B818` - Sends via UART protocol `@Ka<keycode>*`

**Key Event Types** (from `ecovacs_ros_messages.md`):
- KEY_EVENT_CLICK = 0
- KEY_EVENT_DOUBLE_CLICK = 1
- KEY_EVENT_LONG_PRESSED = 2
- KEY_EVENT_LONG_LONG_PRESSED = 3
- KEY_EVENT_PRESSED_DOWN = 4
- KEY_EVENT_PRESSED_UP = 5

---

## SPI2 Interface (Hardware SPI - GD25Q80 Flash)

| Register | Address      | Config                        |
|----------|--------------|-------------------------------|
| Base     | 0x40003800   |                               |
| CR1      | Mode 0x104   | Master, 8-bit, CPOL=0, CPHA=0 |
| CR1      | Prescaler    | 0x200 (fPCLK/4)               |

| Pin  | Signal    | Description                       |
|------|-----------|-----------------------------------|
| PB12 | NSS       | Chip Select (software controlled) |
| PB13 | SCK       | Serial Clock                      |
| PB14 | MISO      | Master In Slave Out               |
| PB15 | MOSI      | Master Out Slave In               |

- Init: `spi2_init` @ `0x8019ED0`
- NSS Toggle: `spi2_nss_toggle` @ `0x8019F7C`
- RCC Code: 1806 (0x70E)

**Note:** Connected to GD25Q80ETIG SPI Flash (1MB). Used for storing display assets, settings, or firmware updates.

---

## SPI Flash Driver API (GD25Q80ETIG)

The external SPI Flash is used **exclusively for OTA firmware updates**, not for UI assets.
UI graphics/fonts are stored in the MCU's internal flash.

### Flash Commands (Standard SPI NOR)

| Address    | Function                    | Opcode | Description                   |
|------------|-----------------------------|--------|-------------------------------|
| 0x8019CE4  | `spi_flash_read_status`     | 0x05   | Read Status Register          |
| 0x8019D06  | `spi_flash_write_enable`    | 0x06   | Write Enable (WREN)           |
| 0x8019D28  | `spi_flash_read_byte`       | 0x03   | Read single byte at address   |
| 0x8019224  | `spi_flash_read_buffer`     | 0x03   | Read buffer (dest, addr, len) |
| 0x8019D62  | `spi_flash_read_u32`        | 0x03   | Read 4 bytes as uint32        |
| 0x8019D96  | `spi_flash_page_program`    | 0x02   | Page Program (max 256 bytes)  |
| 0x8019E42  | `spi_flash_write_u32`       | 0x02   | Write 4 bytes                 |
| 0x8019E72  | `spi_flash_chip_erase`      | 0xC7   | Chip Erase (full 1MB)         |
| 0x8019E94  | `spi_flash_sector_erase_4k` | 0x20   | Sector Erase (4KB)            |
| 0x8019CCC  | `spi_flash_tx_byte`         | -      | Low-level SPI TX              |
| 0x8019CDA  | `spi_flash_rx_byte`         | -      | Low-level SPI RX              |

### OTA Update Functions

| Address    | Function                    | Description                   |
|------------|-----------------------------|-------------------------------|
| 0x8019B18  | `spi_flash_verify_md5`      | Verify firmware with MD5 hash |
| 0x801D12A  | `spi_flash_write_verify`    | Write and read-back verify    |
| 0x801CB9A  | `ota_uart_receive_firmware` | UART firmware receive handler |

### OTA Update Process

1. Linux SBC sends VP message (SA Protocol) to trigger OTA mode
2. MCU enters `ota_uart_receive_firmware` loop
3. Receives firmware in 1024-byte blocks via UART (XMODEM-style)
4. Writes blocks to external SPI Flash
5. After transfer complete, verifies MD5 hash (16 bytes at end)
6. Max firmware size: 256KB (0x40000)

**Note:** The SPI Flash is NOT used for storing icons, fonts, or UI assets.
All display graphics are compiled into the MCU's internal flash (~116KB application space).

---

## UI/Menu System

The Top MCU contains the **complete UI logic**, not just display driving.

### Known UI Pages (from debug strings)

| Page Name         | Debug String                       |
|-------------------|------------------------------------|
| Main Menu         | `enter mainmenu page`              |
| Language Settings | `enter LanguagesSet page`          |
| General Settings  | `enter generalsetting page`        |
| Mow Mode Choose   | `enter mowModeChoose page`         |
| Mode Status       | `enter modestatus page %d`         |
| Return Choose     | `enter ReturnChoose page`          |
| Settings          | `enter SettingOne page %d`         |
| Alarm             | `enter alarm page %d`              |
| Mapping           | `enter maping page %d`             |
| Returning         | `enter returning page`             |
| Scheduled Task    | `enter PlanSkedPrepare page`       |
| Startup           | `enter StartUpPrepare page`        |
| OpenHmi           | `enter OpenHmi page %d`            |
| TmpPage           | `enter TmpPage page %d`            |

### Multi-Language Support

The firmware contains UI strings in multiple languages:
- English, German, French, Spanish, Portuguese
- Italian, Norwegian, Finnish, Swedish, Danish
- And more...

String references for "Rain Sensor":
- `0x80292C4`: "Rain sensor" (EN)
- `0x80299CC`: "RAIN SENSOR" (EN caps)
- `0x8029360`: "Sadeanturi" (FI)

---

## Rain Sensor 🌧️

**Status:** Directly connected to Top MCU on PA1!

### Hardware

| Pin | Function    | Description                              |
|-----|-------------|------------------------------------------|
| PA1 | RAIN_SENSOR | Input with pull-up, LOW = water detected |

### Detection Logic (`rain_sensor_detect_pa1` @ `0x8008EEA`)

```c
// Called every 5ms (task flag 0x01)
void rain_sensor_detect_pa1(void) {
    if (!read_PA1())              // PA1 LOW = water detected
        ++g_rain_counter;
    g_rain_sample_counter++;

    // Every 2000 samples (10 sec) OR threshold exceeded:
    if (g_rain_sample_counter >= 2000 || g_rain_counter > 600) {
        g_rain_sensor_state = (g_rain_counter > 600);  // 1 = rain, 0 = dry
        g_rain_counter = 0;
        g_rain_sample_counter = 0;
    }
}
```

**Threshold:** PA1 LOW for >600 of 2000 samples (~30%) = Rain detected!

### ZR Message (TX to Host, every 1 second)

The Top MCU automatically sends rain status via SA Protocol:

```
Message ID: 'Z' (0x5A), 'R' (0x52)
Payload:    2 bytes
  Byte 0:   g_rain_sensor_subtype (0x20000F9C) - always 0x01 for rain
  Byte 1:   g_rain_sensor_state   (0x20003918) - 0=dry, 1=rain
```

**SA Frame Format:**
```
` + SA + 0x02 + [timestamp] + "ZR" + [subtype=0x01] + [state] + [CRC8] + \n
```

### Key Variables

| Address    | Name                    | Description                   |
|------------|-------------------------|-------------------------------|
| 0x20003918 | `g_rain_sensor_state`   | Rain detected (0=dry, 1=rain) |
| 0x200038E4 | `g_rain_counter`        | Samples with PA1 LOW          |
| 0x200038E6 | `g_rain_sample_counter` | Total samples in window       |
| 0x20000F9C | `g_rain_sensor_subtype` | ZR subtype byte (0x01)        |

### Key Functions

| Address   | Name                     | Description                   |
|-----------|--------------------------|-------------------------------|
| 0x8008EEA | `rain_sensor_detect_pa1` | PA1 polling & threshold check |
| 0x80054EC | `sa_send_zr_rain_status` | Build & send ZR message       |

### Main Loop Integration

```
Timer IRQ (1ms) → timer_task_scheduler()
  ├─ Every 5ms   → g_task_flags |= 0x01  ◄── Rain detection!
  └─ Every 1s    → g_task_flags |= 0x04  ◄── ZR TX!

Main Loop:
  if (g_task_flags & 0x01) {      // Every 5ms
      rain_sensor_detect_pa1();   // ◄── Rain detection!
  }
  
  if (g_task_flags & 0x04) {      // Every 1 second
      sa_send_zr_rain_status(subtype, state);  // ◄── ZR TX!
  }
```

### ROS Integration

The host ROS node receives ZR messages and publishes to:
- **Topic:** `onOffInfo/RainDetectState`
- **Message Type:** Custom message with `NOT_RAIN_STATE=0`, `RAIN_STATE=1`

---

## Peripheral Clock Configuration

RCC enable codes used by `rcc_periph_clock_enable` @ `0x800BC16`:

| Code (dec) | Code (hex) | Peripheral |
|------------|------------|------------|
| 1538       | 0x602      | GPIOA      |
| 1539       | 0x603      | GPIOB      |
| 1550       | 0x60E      | USART1     |
| 1806       | 0x70E      | SPI2       |
| 1809       | 0x711      | USART2     |
| 1797       | 0x705      | TIM6       |

---

## Key IDA Labels (set for navigation)

### Core Functions
| Address    | Label                      | Description                           |
|------------|----------------------------|---------------------------------------|
| 0x8021278  | `main`                     | Entry point / main loop               |
| 0x801F294  | `clock_config`             | System clock configuration            |
| 0x8008DA6  | `gpio_init_ui_pins`        | All UI GPIO initialization            |
| 0x8008CA4  | `gpio_init_display_pins`   | Display GPIO setup                    |
| 0x8008D04  | `gpio_init_button_pins`    | Button GPIO setup                     |

### Display Functions
| Address    | Label                      | Description                           |
|------------|----------------------------|---------------------------------------|
| 0x8008918  | `display_init_st7565`      | ST7565 init sequence                  |
| 0x8008864  | `spi_bitbang_tx_byte`      | Bit-bang SPI transmit                 |
| 0x8008896  | `display_send_byte`        | Send byte to display                  |

### Button/Key Functions
| Address    | Label                        | Description                         |
|------------|------------------------------|-------------------------------------|
| 0x8008E46  | `key_debounce_scan`          | Button debounce scanning            |
| 0x8008EEA  | `rain_sensor_detect_pa1`     | PA1 rain sensor detection           |
| 0x8008F44  | `key_update_keycodes`        | Generate keycodes                   |
| 0x801B818  | `send_pending_key_event`     | Send key event via UART             |
| 0x801B7C0  | `relay_mode_send_rain_state` | Send rain sensor state (Relay Mode) |

### UART Functions
| Address    | Label                      | Description                           |
|------------|----------------------------|---------------------------------------|
| 0x801F83C  | `usart1_init`              | USART1 initialization                 |
| 0x800A88C  | `usart2_init`              | USART2 initialization                 |
| 0x800AB96  | `usart2_irq_handler`       | USART2 RX interrupt handler           |
| 0x800AA2A  | `usart2_send_buffer`       | Send buffer via USART2                |

### SA Protocol Functions
| Address    | Label                      | Description                           |
|------------|----------------------------|---------------------------------------|
| 0x801B178  | `sa_command_handler`       | Main command dispatcher (RX)          |
| 0x801ECFA  | `sa_message_dispatch`      | Dispatch incoming SA messages         |
| 0x801BA26  | `sa_parse_hex_byte`        | Parse hex byte from ASCII             |
| 0x801BA3A  | `sa_response_builder_alloc`| Allocate response builder             |
| 0x801BB6E  | `sa_response_builder_add_str` | Add string to response             |
| 0x801BBE6  | `sa_response_builder_add_hex` | Add hex to response                |
| 0x801BA7C  | `sa_response_builder_free` | Free response builder                 |

### SA Protocol TX Functions (Message Sending)
| Address    | Label                      | Description                           |
|------------|----------------------------|---------------------------------------|
| 0x80083A2  | `ui_text_obj_create`       | Create text object with msg ID        |
| 0x800843E  | `ui_text_obj_set_text`     | Set payload data                      |
| 0x8008604  | `ui_text_obj_draw`         | Trigger TX via SA wrapper             |
| 0x80083D2  | `sa_rx_frame_parse`        | Parse incoming SA frame               |
| 0x8008564  | `sa_tx_serialize_text_obj` | Serialize text obj to buffer          |
| 0x8008656  | `sa_tx_frame_create`       | Create SA TX frame ('S','A')          |
| 0x8008674  | `sa_tx_frame_set_payload`  | Set frame payload                     |
| 0x80086AE  | `sa_tx_frame_send`         | Send frame via USART2                 |
| 0x8008692  | `sa_tx_frame_destroy`      | Free TX frame memory                  |

### SA Message Senders (ZC, ZE, etc.)
| Address    | Label                      | Description                           |
|------------|----------------------------|---------------------------------------|
| 0x8005308  | `sa_send_zc_unlock_event`  | Send ZC (correct PIN entered)         |
| 0x800537A  | `sa_send_zc_unlock_error`  | Send ZC (wrong PIN)                   |
| 0x80053D0  | `sa_send_ze_reset_event`   | Send ZE (screen reset)                |

### SPI Flash Functions
| Address    | Label                      | Description                           |
|------------|----------------------------|---------------------------------------|
| 0x8019ED0  | `spi2_init`                | SPI2 peripheral init                  |
| 0x8019F7C  | `spi2_nss_toggle`          | SPI2 NSS control                      |
| 0x8019CE4  | `spi_flash_read_status`    | Read flash status register            |
| 0x8019D06  | `spi_flash_write_enable`   | Flash write enable                    |
| 0x8019D28  | `spi_flash_read_byte`      | Read single byte                      |
| 0x8019224  | `spi_flash_read_buffer`    | Read buffer                           |
| 0x8019D62  | `spi_flash_read_u32`       | Read 32-bit value                     |
| 0x8019D96  | `spi_flash_page_program`   | Page program                          |
| 0x8019E42  | `spi_flash_write_u32`      | Write 32-bit value                    |
| 0x8019E72  | `spi_flash_chip_erase`     | Chip erase                            |
| 0x8019E94  | `spi_flash_sector_erase_4k`| Sector erase (4KB)                    |
| 0x8019B18  | `spi_flash_verify_md5`     | MD5 verification                      |
| 0x801D12A  | `spi_flash_write_verify`   | Write with verification               |
| 0x801CB9A  | `ota_uart_receive_firmware`| OTA firmware receiver                 |

### Helper Functions
| Address    | Label                      | Description                           |
|------------|----------------------------|---------------------------------------|
| 0x800BC16  | `rcc_periph_clock_enable`  | RCC clock enable helper               |
| 0x800BAE4  | `gpio_init_pin`            | GPIO pin configuration                |
| 0x800BBA0  | `gpio_read_input_bit`      | Read GPIO input state                 |
| 0x80214BC  | `timer6_init`              | TIM6 initialization                   |
| 0x8021538  | `nvic_init`                | NVIC interrupt setup                  |
| 0x80215E4  | `backup_register_check`    | Backup domain validation              |
| 0x8021774  | `rcc_reset`                | RCC reset sequence                    |

### Global Variables
| Address    | Label                      | Description                               |
|------------|----------------------------|-------------------------------------------|
| 0x20003194 | `g_sa_rx_buffer`           | SA command receive buffer                 |
| 0x20003941 | `g_sa_cmd_ready`           | Command ready flag                        |
| 0x20003942 | `g_fct_detect_state`       | FCT mode detection state                  |
| 0x20003943 | `g_fct_mode_active`        | FCT mode active flag                      |
| 0x2000391A | `g_display_mode`           | UI render gate: 0 normal, 1 FCT, 2 update |
| 0x20003936 | `g_mode_display`           | Display mode flag (set by `@Da` in FCT)   |
| 0x20003937 | `g_mode_relay`             | Relay mode state                          |
| 0x20003938 | `g_mode_keyevent`          | Key event mode state                      |
| 0x20003939 | `g_mode_screen`            | Screen mode state                         |
| 0x20003918 | `g_rain_sensor_state`      | Rain sensor state (0=dry, 1=rain)         |

---

## Memory Map

| Region          | Start      | End        | Description                          |
|-----------------|------------|------------|--------------------------------------|
| Flash           | 0x08000000 | 0x0801FFFF | 128KB Flash                          |
| Bootloader      | 0x08000000 | 0x08002FFF | 12KB Bootloader                      |
| Application     | 0x08003000 | 0x0801FFFF | ~116KB Application                   |
| SRAM            | 0x20000000 | 0x20004FFF | 20KB SRAM                            |
| Peripherals     | 0x40000000 | 0x40023FFF | APB/AHB Peripherals                  |
| Ext. SPI Flash  | (via SPI2) | -          | 1MB GD25Q80 (OTA only)               |

---

## Notes for Custom Firmware Development

1. **Display**: ST7565/UC1701 compatible, 128x64 pixels, bit-banged SPI
2. **Buttons**: 8 buttons + reset, active-low with internal pull-ups
3. **Communication**: USART2 @ 115200 baud to main SBC (ASCII-wrapped SA Protocol)
4. **External Flash**: GD25Q80 on SPI2 - **OTA updates only**, not UI assets
5. **UI Assets**: Stored in internal MCU flash (~116KB), not external flash
6. **USB**: PA11/PA12 available but unused - could enable USB CDC
7. **Debug**: PA13/PA14 are SWDIO/SWCLK - keep available for SWD
8. **Menu Logic**: Complete UI state machine runs on Top MCU, not Linux SBC

---

