# NOIR SENTINEL — STM32 RFID Access Control

<p align="center">
  <img src="https://img.shields.io/badge/STM32-STM32F091RC-03234B?style=for-the-badge&logo=stmicroelectronics&logoColor=white" alt="STM32F091RC">
  <img src="https://img.shields.io/badge/Firmware-C%20%2F%20STM32%20HAL-blue?style=for-the-badge" alt="C STM32 HAL">
  <img src="https://img.shields.io/badge/Project-Embedded%20Systems-black?style=for-the-badge" alt="Embedded Systems">
</p>

<p align="center">
  <i>An embedded access-control prototype combining RFID, capacitive keypad input, OLED feedback, and audible alerts.</i>
</p>

<p align="center">
  <img src="https://raw.githubusercontent.com/Templarian/MaterialDesign/master/svg/robot-happy.svg" width="100" alt="Floating AI robot decoration">
  &nbsp;&nbsp;&nbsp;
  <img src="https://raw.githubusercontent.com/Templarian/MaterialDesign/master/svg/robot.svg" width="135" alt="NOIR AI robot">
  &nbsp;&nbsp;&nbsp;
  <img src="https://raw.githubusercontent.com/Templarian/MaterialDesign/master/svg/robot-excited.svg" width="100" alt="Floating AI robot decoration">
</p>

---

## 1. Project Overview

**NOIR SENTINEL** is an STM32-based access-control prototype designed to explore low-level firmware, peripheral communication, and embedded user interfaces.

The system integrates an RFID reader, a capacitive keypad, an OLED display, and a buzzer with an STM32F091RC microcontroller.

### Features

- RFID card detection through the RC522 module.
- PIN entry using a TTP229 capacitive keypad interface.
- SSD1306 OLED status display over I²C.
- Buzzer alerts for user feedback.
- Temporary lockout logic after unsuccessful PIN attempts.
- UART interface for serial debugging.
- STM32CubeIDE project configuration and STM32 HAL firmware.

## 2. System Architecture

```text
                 NOIR SENTINEL
                       |
               STM32F091RC MCU
                       |
       +---------------+----------------+
       |               |                |
    SPI1 / CS        I2C1             GPIO
       |               |                |
    RC522 RFID      SSD1306         TTP229 Keypad
                                        |
                                    PIN Input

       +----------------+----------------+
       |                                 |
    GPIO Output                       USART2
       |                                 |
     Buzzer                         Serial Debug
```

## 3. Hardware Components

| Component | Purpose | Interface |
|---|---|---|
| STM32F091RC | Main controller | MCU |
| RC522 RFID module | RFID card detection | SPI |
| TTP229 keypad module | Capacitive key input | GPIO |
| SSD1306 OLED | Status and authentication messages | I²C |
| Buzzer | Audible feedback | GPIO |
| USB-to-UART / supported debug interface | Serial monitoring | USART2 |

> **Important:** Confirm the exact module variants and their supply-voltage requirements before wiring. The pin assignments below describe the current firmware design, not a guarantee that every breakout board has the same connector layout.

## 4. Wiring and Pin Mapping

### 4.1 RC522 RFID — SPI1

| RC522 pin | STM32F091RC pin | Description |
|---|---|---|
| SDA / SS | PB6 | Chip Select |
| SCK | PA5 | SPI1 Clock |
| MOSI | PA7 | SPI1 Master Out |
| MISO | PA6 | SPI1 Master In |
| RST | PC7 | Reset |
| GND | GND | Common ground |
| 3.3V | 3V3 | Power supply, if supported by the module |

**Wiring notes**

1. Connect RC522 `SCK` to `PA5`.
2. Connect `MOSI` to `PA7` and `MISO` to `PA6`.
3. Connect `SDA/SS` to `PB6`; on the RC522, this pin is the SPI chip-select signal, not the OLED I²C SDA line.
4. Connect `RST` to `PC7`.
5. Connect ground to ground and use a suitable 3.3 V supply.

**Warning:** Do not connect the RC522 to 5 V logic signals. Verify the module's power requirements before powering it.

### 4.2 SSD1306 OLED — I²C1

| OLED pin | STM32F091RC pin | Description |
|---|---|---|
| SCL | PB8 | I²C Clock |
| SDA | PB9 | I²C Data |
| GND | GND | Common ground |
| VCC | Compatible supply | Follow the breakout-board specification |

The firmware expects the common SSD1306 I²C address `0x3C`. Confirm the address and I²C configuration if the screen remains blank.

### 4.3 TTP229 Capacitive Keypad

The current firmware pin map assigns eight key inputs to GPIO pins.

| Key input | STM32F091RC pin |
|---|---|
| KEY1 | PA0 |
| KEY2 | PA1 |
| KEY3 | PA4 |
| KEY4 | PB0 |
| KEY5 | PC1 |
| KEY6 | PC0 |
| KEY7 | PC2 |
| KEY8 | PC3 |

**Wiring notes**

- Match each keypad output to the corresponding STM32 GPIO.
- Connect the keypad ground to the STM32 ground.
- Power the module according to its specific datasheet.
- Confirm whether the keypad outputs active-high or active-low signals.
- The firmware GPIO configuration must match the actual output behavior.

> TTP229 boards exist in different configurations. Confirm that your particular board exposes eight separate GPIO key outputs before using this pin map. Some variants use a serial interface instead.

### 4.4 Buzzer

| Buzzer connection | STM32F091RC pin |
|---|---|
| Signal / positive input | PA9 |
| GND / negative input | GND |

The current firmware uses `PA9` as the buzzer control output.

Check whether the buzzer is active-high and whether it can be driven directly from a GPIO. If the buzzer current exceeds the MCU pin's rating, use an appropriate transistor driver.

### 4.5 USART2 Serial Debugging

| STM32F091RC pin | Connection |
|---|---|
| PA2 | UART TX |
| PA3 | UART RX |
| GND | Common ground |

For a USB-to-UART adapter:

- STM32 `PA2 / TX` → adapter `RX`.
- STM32 `PA3 / RX` → adapter `TX`.
- STM32 `GND` → adapter `GND`.
- Configure the serial terminal to **115200 baud**, using the UART settings configured in firmware.

Verify that the adapter uses compatible 3.3 V logic. Do not connect RS-232 voltage levels directly to STM32 GPIO pins.

## 5. Complete Wiring Reference

```text
STM32F091RC
│
├── SPI1
│   ├── PA5 ───────── RC522 SCK
│   ├── PA6 ───────── RC522 MISO
│   ├── PA7 ───────── RC522 MOSI
│   ├── PB6 ───────── RC522 SDA/SS
│   └── PC7 ───────── RC522 RST
│
├── I2C1
│   ├── PB8 ───────── SSD1306 SCL
│   └── PB9 ───────── SSD1306 SDA
│
├── Keypad GPIO
│   ├── PA0 ───────── TTP229 KEY1
│   ├── PA1 ───────── TTP229 KEY2
│   ├── PA4 ───────── TTP229 KEY3
│   ├── PB0 ───────── TTP229 KEY4
│   ├── PC1 ───────── TTP229 KEY5
│   ├── PC0 ───────── TTP229 KEY6
│   ├── PC2 ───────── TTP229 KEY7
│   └── PC3 ───────── TTP229 KEY8
│
├── PA9 ───────────── Buzzer signal
│
└── USART2
    ├── PA2 / TX ──── USB-UART RX
    └── PA3 / RX ──── USB-UART TX

All modules must share a compatible common ground.
Power each module using its specified voltage.
```

## 6. How to Wire the Circuit Safely

1. **Disconnect power** before inserting or moving wires.
2. Place the STM32 development board on a stable, non-conductive surface.
3. Wire the ground connections first.
4. Connect the RC522 SPI signals one by one.
5. Connect the OLED's I²C signals.
6. Connect each keypad output to its assigned GPIO.
7. Connect the buzzer after confirming its voltage and current requirements.
8. Add the UART adapter only if serial monitoring is needed.
9. Check for reversed power connections, short circuits, and mismatched voltage levels.
10. Power the circuit and test one module at a time.

**Recommended testing order**

- Test the OLED first.
- Verify UART output.
- Test the keypad and confirm each GPIO input.
- Test RC522 card detection.
- Test buzzer feedback.
- Integrate all modules and test the authentication workflow.

## 7. Firmware Setup

### Requirements

- STM32CubeIDE
- STM32CubeF0 HAL
- Compatible STM32F091RC development board
- ST-LINK or the supported programming/debugging interface

### Open the project

1. Clone or download this repository.
2. Open STM32CubeIDE.
3. Select **File → Import → Existing Projects into Workspace**.
4. Select the repository directory.
5. Build the project.
6. Connect the supported programming interface and flash the firmware.

The repository includes the CubeIDE project metadata, `.ioc` configuration, startup code, HAL drivers, and application source.

## 8. Authentication Behavior

The intended demonstration workflow is:

1. Detect an RFID card.
2. Provide feedback through the OLED and buzzer.
3. Accept PIN input from the capacitive keypad.
4. Display an access result.
5. Apply a temporary lockout after repeated incorrect PIN attempts.

Actual behavior depends on the implementation in `Core/Src/main.c`.

### Security Notice

This project is an educational prototype, **not a production-ready security system**.

If `RFID_ACCEPT_ANY_CARD` is enabled, any readable card may be accepted by the demonstration logic. Before using this design for real access control, implement explicit UID authorization, secure PIN handling, appropriate lockout and recovery behavior, and a threat model for physical access.

Do not rely on the current demonstration firmware to protect valuables or secure premises.

## 9. Troubleshooting

| Symptom | What to check |
|---|---|
| OLED stays blank | I²C address, PB8/PB9 wiring, supply voltage, and I²C initialization |
| RC522 does not detect cards | SPI pin mapping, chip select, reset, 3.3 V supply, and supported card type |
| Keypad does not respond | Actual TTP229 variant, GPIO mapping, output polarity, and GPIO configuration |
| Buzzer stays silent | Buzzer polarity, driver circuit, GPIO configuration, and supply |
| UART output is unreadable | Baud rate, TX/RX crossover, common ground, and logic voltage |
| Firmware does not build | Project import, selected MCU, toolchain, and CubeIDE configuration |

## 10. Project Structure

```text
STM32_rfid/
├── Core/
│   ├── Inc/
│   ├── Src/
│   │   └── main.c
│   └── Startup/
├── Drivers/
│   ├── CMSIS/
│   └── STM32F0xx_HAL_Driver/
├── .settings/
├── STM32F091RCTX_FLASH.ld
├── STM32_rfid.ioc
├── .gitignore
└── README.md
```

## 11. Future Improvements

- Add a verified RFID UID allowlist.
- Improve keypad debouncing and input handling.
- Add clearer authentication and lockout states.
- Separate peripheral drivers from application logic.
- Add wiring diagrams and hardware test documentation.
- Integrate a display interface and a separate communication controller.

---

<p align="center">
  <img src="https://raw.githubusercontent.com/Templarian/MaterialDesign/master/svg/robot-happy.svg" width="64" alt="AI robot">
  <br>
  <b>NOIR SENTINEL</b>
  <br>
  <i>Learn deeply. Build reliably. Engineer beyond the prototype.</i>
</p>
