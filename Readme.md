# Intellivision Bluetooth Gamepad Controller

This project is a firmware for an ESP32 board that makes it possible to use an old Intellivision 1-3 controller as a Bluetooth gamepad with any device that supports Bluetooth HID gamepads (PC, Android, iOS, etc.).

## Features

- Bluetooth HID gamepad support
- Battery level monitoring and reporting
- Power saving with automatic sleep mode
- Status LED indicators
- Support for all original controller functions
- Low battery protection
- Analog stick support
- Multiple controller profiles
- Button combinations for special functions

## The hardware

The hardware is very simple. You just need:
1. An ESP32 development board (like ESP32 DevKit, NodeMCU-32S, etc.)
2. One original Intellivision controller
3. Some jumper wires for connections
4. (Optional) A LiPo battery and charging circuit for portable use

Connect the original controller connector to the ESP32 as follows:
- Brown wire (Ground) -> ESP32 GND
- Pin 1 -> ESP32 GPIO 5
- Pin 2 -> ESP32 GPIO 4
- Pin 3 -> ESP32 GPIO 3
- Pin 4 -> ESP32 GPIO 2
- Pin 6 -> ESP32 GPIO 9
- Pin 7 -> ESP32 GPIO 8
- Pin 8 -> ESP32 GPIO 7
- Pin 9 -> ESP32 GPIO 6

For battery monitoring (optional):
- Battery voltage -> ESP32 GPIO 34 (ADC)

## Required Libraries

Before uploading the code, you need to install these libraries in the Arduino IDE:
1. "ESP32 BLE Gamepad" by lemmingDev
2. "Arduino" (should be installed by default)
3. "Preferences" (included with ESP32 board support)

## Flashing the firmware

1. Install the ESP32 board support in Arduino IDE:
   - Go to File -> Preferences
   - Add `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json` to Additional Board Manager URLs
   - Go to Tools -> Board -> Boards Manager
   - Search for "esp32" and install "ESP32 by Espressif Systems"

2. Select your ESP32 board:
   - Go to Tools -> Board -> ESP32 Arduino
   - Select your specific ESP32 board model

3. Upload the code:
   - Connect your ESP32 to your computer
   - Select the correct port in Tools -> Port
   - Click the Upload button

## Using the controller

1. After uploading, the ESP32 will start advertising as a Bluetooth gamepad named "Intellivision Controller"
2. On your device (PC, phone, etc.), go to Bluetooth settings and pair with the controller
3. The controller will appear as a standard gamepad with:
   - 16 direction buttons (mapped to the disc)
   - 12 keypad buttons (0-9, Clear, Enter)
   - 3 side buttons
   - Analog stick support (when enabled)

## Advanced Features

### Analog Stick Support

The controller can operate in two modes:
1. **Digital Mode (Default)**: The disc acts as 16 digital direction buttons
2. **Analog Mode**: The disc acts as an analog stick with 16 positions

To toggle between modes:
- Press Side Buttons 2+3 simultaneously

### Multiple Profiles

The controller supports up to 4 different profiles, each with its own button mapping and settings.

To switch profiles:
- Press Side Buttons 1+2 simultaneously

To save current profile:
- Press Side Buttons 1+3 simultaneously

### Button Combinations

The following button combinations are available:
- Side Buttons 1+2: Switch to next profile
- Side Buttons 2+3: Toggle analog/digital mode
- Side Buttons 1+3: Save current profile

## Power Management

The controller includes several power-saving features:

1. **Automatic Sleep Mode**:
   - Controller enters sleep mode after 5 minutes of inactivity
   - Press any button to wake up
   - Sleep mode can be disabled by setting IDLE_TIMEOUT to 0

2. **Battery Monitoring**:
   - Battery level is reported to the connected device
   - Controller enters sleep mode when battery is critically low (< 10%)
   - Battery level is checked every 60 seconds

3. **Status Indicators**:
   - Built-in LED shows connection status:
     - Blinking: Not connected
     - Solid on: Connected
     - Off: Sleep mode

## Button Mapping

The controller maps to standard gamepad buttons as follows:

- Direction disc: Buttons 1-16 (clockwise from North)
- Keypad: Buttons 17-28 (1-9, Clear, 0, Enter)
- Side buttons: Buttons 29-31

In analog mode, the disc also controls the left analog stick with 16 positions.

## Troubleshooting

If the controller doesn't connect:
1. Make sure Bluetooth is enabled on your device
2. Try resetting the ESP32 (press the reset button)
3. Check that all wires are properly connected
4. Verify the ESP32 is powered properly
5. Check the status LED for connection state

For connection issues, you can monitor the Serial output at 115200 baud to see debug messages.

## Power Consumption

Typical power consumption:
- Active use: ~100mA
- Connected but idle: ~50mA
- Sleep mode: < 1mA

With a 1000mAh battery, you can expect:
- 10 hours of active use
- 20 hours of connected idle time
- Weeks in sleep mode
